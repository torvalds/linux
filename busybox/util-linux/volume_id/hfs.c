/*
 * volume_id - reads filesystem label and uuid
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 *
 *	This library is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU Lesser General Public
 *	License as published by the Free Software Foundation; either
 *	version 2.1 of the License, or (at your option) any later version.
 *
 *	This library is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *	Lesser General Public License for more details.
 *
 *	You should have received a copy of the GNU Lesser General Public
 *	License along with this library; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
//config:config FEATURE_VOLUMEID_HFS
//config:	bool "hfs filesystem"
//config:	default y
//config:	depends on VOLUMEID

//kbuild:lib-$(CONFIG_FEATURE_VOLUMEID_HFS) += hfs.o

#include "volume_id_internal.h"

struct hfs_finder_info{
	uint32_t	boot_folder;
	uint32_t	start_app;
	uint32_t	open_folder;
	uint32_t	os9_folder;
	uint32_t	reserved;
	uint32_t	osx_folder;
	uint8_t		id[8];
} PACKED;

struct hfs_mdb {
	uint8_t		signature[2];
	uint32_t	cr_date;
	uint32_t	ls_Mod;
	uint16_t	atrb;
	uint16_t	nm_fls;
	uint16_t	vbm_st;
	uint16_t	alloc_ptr;
	uint16_t	nm_al_blks;
	uint32_t	al_blk_size;
	uint32_t	clp_size;
	uint16_t	al_bl_st;
	uint32_t	nxt_cnid;
	uint16_t	free_bks;
	uint8_t		label_len;
	uint8_t		label[27];
	uint32_t	vol_bkup;
	uint16_t	vol_seq_num;
	uint32_t	wr_cnt;
	uint32_t	xt_clump_size;
	uint32_t	ct_clump_size;
	uint16_t	num_root_dirs;
	uint32_t	file_count;
	uint32_t	dir_count;
	struct hfs_finder_info finder_info;
	uint8_t		embed_sig[2];
	uint16_t	embed_startblock;
	uint16_t	embed_blockcount;
} PACKED;

struct hfsplus_bnode_descriptor {
	uint32_t	next;
	uint32_t	prev;
	uint8_t		type;
	uint8_t		height;
	uint16_t	num_recs;
	uint16_t	reserved;
} PACKED;

struct hfsplus_bheader_record {
	uint16_t	depth;
	uint32_t	root;
	uint32_t	leaf_count;
	uint32_t	leaf_head;
	uint32_t	leaf_tail;
	uint16_t	node_size;
} PACKED;

struct hfsplus_catalog_key {
	uint16_t	key_len;
	uint32_t	parent_id;
	uint16_t	unicode_len;
	uint8_t		unicode[255 * 2];
} PACKED;

struct hfsplus_extent {
	uint32_t	start_block;
	uint32_t	block_count;
} PACKED;

#define HFSPLUS_EXTENT_COUNT		8
struct hfsplus_fork {
	uint64_t	total_size;
	uint32_t	clump_size;
	uint32_t	total_blocks;
	struct hfsplus_extent extents[HFSPLUS_EXTENT_COUNT];
} PACKED;

struct hfsplus_vol_header {
	uint8_t		signature[2];
	uint16_t	version;
	uint32_t	attributes;
	uint32_t	last_mount_vers;
	uint32_t	reserved;
	uint32_t	create_date;
	uint32_t	modify_date;
	uint32_t	backup_date;
	uint32_t	checked_date;
	uint32_t	file_count;
	uint32_t	folder_count;
	uint32_t	blocksize;
	uint32_t	total_blocks;
	uint32_t	free_blocks;
	uint32_t	next_alloc;
	uint32_t	rsrc_clump_sz;
	uint32_t	data_clump_sz;
	uint32_t	next_cnid;
	uint32_t	write_count;
	uint64_t	encodings_bmp;
	struct hfs_finder_info finder_info;
	struct hfsplus_fork alloc_file;
	struct hfsplus_fork ext_file;
	struct hfsplus_fork cat_file;
	struct hfsplus_fork attr_file;
	struct hfsplus_fork start_file;
} PACKED;

#define HFS_SUPERBLOCK_OFFSET		0x400
#define HFS_NODE_LEAF			0xff
#define HFSPLUS_POR_CNID		1

static void FAST_FUNC hfs_set_uuid(struct volume_id *id, const uint8_t *hfs_id)
{
#define hfs_id_len 8
	md5_ctx_t md5c;
	uint8_t uuid[16];
	unsigned i;

	for (i = 0; i < hfs_id_len; i++)
		if (hfs_id[i] != 0)
			goto do_md5;
	return;
 do_md5:
	md5_begin(&md5c);
	md5_hash(&md5c, "\263\342\17\71\362\222\21\326\227\244\0\60\145\103\354\254", 16);
	md5_hash(&md5c, hfs_id, hfs_id_len);
	md5_end(&md5c, uuid);
	uuid[6] = 0x30 | (uuid[6] & 0x0f);
	uuid[8] = 0x80 | (uuid[8] & 0x3f);
	volume_id_set_uuid(id, uuid, UUID_DCE);
}

int FAST_FUNC volume_id_probe_hfs_hfsplus(struct volume_id *id /*,uint64_t off*/)
{
	uint64_t off = 0;
	unsigned blocksize;
	unsigned cat_block;
	unsigned ext_block_start;
	unsigned ext_block_count;
	int ext;
	unsigned leaf_node_head;
	unsigned leaf_node_count;
	unsigned leaf_node_size;
	unsigned leaf_block;
	uint64_t leaf_off;
	unsigned alloc_block_size;
	unsigned alloc_first_block;
	unsigned embed_first_block;
	unsigned record_count;
	struct hfsplus_vol_header *hfsplus;
	struct hfsplus_bnode_descriptor *descr;
	struct hfsplus_bheader_record *bnode;
	struct hfsplus_catalog_key *key;
	unsigned label_len;
	struct hfsplus_extent extents[HFSPLUS_EXTENT_COUNT];
	struct hfs_mdb *hfs;
	const uint8_t *buf;

	dbg("probing at offset 0x%llx", (unsigned long long) off);

	buf = volume_id_get_buffer(id, off + HFS_SUPERBLOCK_OFFSET, 0x200);
	if (buf == NULL)
		return -1;

	hfs = (struct hfs_mdb *) buf;
	if (hfs->signature[0] != 'B' || hfs->signature[1] != 'D')
		goto checkplus;

	/* it may be just a hfs wrapper for hfs+ */
	if (hfs->embed_sig[0] == 'H' && hfs->embed_sig[1] == '+') {
		alloc_block_size = be32_to_cpu(hfs->al_blk_size);
		dbg("alloc_block_size 0x%x", alloc_block_size);

		alloc_first_block = be16_to_cpu(hfs->al_bl_st);
		dbg("alloc_first_block 0x%x", alloc_first_block);

		embed_first_block = be16_to_cpu(hfs->embed_startblock);
		dbg("embed_first_block 0x%x", embed_first_block);

		off += (alloc_first_block * 512) +
		       (embed_first_block * alloc_block_size);
		dbg("hfs wrapped hfs+ found at offset 0x%llx", (unsigned long long) off);

		buf = volume_id_get_buffer(id, off + HFS_SUPERBLOCK_OFFSET, 0x200);
		if (buf == NULL)
			return -1;
		goto checkplus;
	}

	if (hfs->label_len > 0 && hfs->label_len < 28) {
//		volume_id_set_label_raw(id, hfs->label, hfs->label_len);
		volume_id_set_label_string(id, hfs->label, hfs->label_len) ;
	}

	hfs_set_uuid(id, hfs->finder_info.id);
//	volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
	IF_FEATURE_BLKID_TYPE(id->type = "hfs";)

	return 0;

 checkplus:
	hfsplus = (struct hfsplus_vol_header *) buf;
	if (hfs->signature[0] == 'H')
		if (hfs->signature[1] == '+' || hfs->signature[1] == 'X')
			goto hfsplus;
	return -1;

 hfsplus:
	hfs_set_uuid(id, hfsplus->finder_info.id);

	blocksize = be32_to_cpu(hfsplus->blocksize);
	dbg("blocksize %u", blocksize);

	memcpy(extents, hfsplus->cat_file.extents, sizeof(extents));
	cat_block = be32_to_cpu(extents[0].start_block);
	dbg("catalog start block 0x%x", cat_block);

	buf = volume_id_get_buffer(id, off + (cat_block * blocksize), 0x2000);
	if (buf == NULL)
		goto found;

	bnode = (struct hfsplus_bheader_record *)
		&buf[sizeof(struct hfsplus_bnode_descriptor)];

	leaf_node_head = be32_to_cpu(bnode->leaf_head);
	dbg("catalog leaf node 0x%x", leaf_node_head);

	leaf_node_size = be16_to_cpu(bnode->node_size);
	dbg("leaf node size 0x%x", leaf_node_size);

	leaf_node_count = be32_to_cpu(bnode->leaf_count);
	dbg("leaf node count 0x%x", leaf_node_count);
	if (leaf_node_count == 0)
		goto found;

	leaf_block = (leaf_node_head * leaf_node_size) / blocksize;

	/* get physical location */
	for (ext = 0; ext < HFSPLUS_EXTENT_COUNT; ext++) {
		ext_block_start = be32_to_cpu(extents[ext].start_block);
		ext_block_count = be32_to_cpu(extents[ext].block_count);
		dbg("extent start block 0x%x, count 0x%x", ext_block_start, ext_block_count);

		if (ext_block_count == 0)
			goto found;

		/* this is our extent */
		if (leaf_block < ext_block_count)
			break;

		leaf_block -= ext_block_count;
	}
	if (ext == HFSPLUS_EXTENT_COUNT)
		goto found;
	dbg("found block in extent %i", ext);

	leaf_off = (ext_block_start + leaf_block) * blocksize;

	buf = volume_id_get_buffer(id, off + leaf_off, leaf_node_size);
	if (buf == NULL)
		goto found;

	descr = (struct hfsplus_bnode_descriptor *) buf;
	dbg("descriptor type 0x%x", descr->type);

	record_count = be16_to_cpu(descr->num_recs);
	dbg("number of records %u", record_count);
	if (record_count == 0)
		goto found;

	if (descr->type != HFS_NODE_LEAF)
		goto found;

	key = (struct hfsplus_catalog_key *)
		&buf[sizeof(struct hfsplus_bnode_descriptor)];

	dbg("parent id 0x%x", be32_to_cpu(key->parent_id));
	if (key->parent_id != cpu_to_be32(HFSPLUS_POR_CNID))
		goto found;

	label_len = be16_to_cpu(key->unicode_len) * 2;
	dbg("label unicode16 len %i", label_len);
//	volume_id_set_label_raw(id, key->unicode, label_len);
	volume_id_set_label_unicode16(id, key->unicode, BE, label_len);

 found:
//	volume_id_set_usage(id, VOLUME_ID_FILESYSTEM);
	IF_FEATURE_BLKID_TYPE(id->type = "hfsplus";)

	return 0;
}
