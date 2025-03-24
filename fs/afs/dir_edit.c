// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS filesystem directory editing
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/iversion.h>
#include <linux/folio_queue.h>
#include "internal.h"
#include "xdr_fs.h"

/*
 * Find a number of contiguous clear bits in a directory block bitmask.
 *
 * There are 64 slots, which means we can load the entire bitmap into a
 * variable.  The first bit doesn't count as it corresponds to the block header
 * slot.  nr_slots is between 1 and 9.
 */
static int afs_find_contig_bits(union afs_xdr_dir_block *block, unsigned int nr_slots)
{
	u64 bitmap;
	u32 mask;
	int bit, n;

	bitmap  = (u64)block->hdr.bitmap[0] << 0 * 8;
	bitmap |= (u64)block->hdr.bitmap[1] << 1 * 8;
	bitmap |= (u64)block->hdr.bitmap[2] << 2 * 8;
	bitmap |= (u64)block->hdr.bitmap[3] << 3 * 8;
	bitmap |= (u64)block->hdr.bitmap[4] << 4 * 8;
	bitmap |= (u64)block->hdr.bitmap[5] << 5 * 8;
	bitmap |= (u64)block->hdr.bitmap[6] << 6 * 8;
	bitmap |= (u64)block->hdr.bitmap[7] << 7 * 8;
	bitmap >>= 1; /* The first entry is metadata */
	bit = 1;
	mask = (1 << nr_slots) - 1;

	do {
		if (sizeof(unsigned long) == 8)
			n = ffz(bitmap);
		else
			n = ((u32)bitmap) != 0 ?
				ffz((u32)bitmap) :
				ffz((u32)(bitmap >> 32)) + 32;
		bitmap >>= n;
		bit += n;

		if ((bitmap & mask) == 0) {
			if (bit > 64 - nr_slots)
				return -1;
			return bit;
		}

		n = __ffs(bitmap);
		bitmap >>= n;
		bit += n;
	} while (bitmap);

	return -1;
}

/*
 * Set a number of contiguous bits in the directory block bitmap.
 */
static void afs_set_contig_bits(union afs_xdr_dir_block *block,
				int bit, unsigned int nr_slots)
{
	u64 mask;

	mask = (1 << nr_slots) - 1;
	mask <<= bit;

	block->hdr.bitmap[0] |= (u8)(mask >> 0 * 8);
	block->hdr.bitmap[1] |= (u8)(mask >> 1 * 8);
	block->hdr.bitmap[2] |= (u8)(mask >> 2 * 8);
	block->hdr.bitmap[3] |= (u8)(mask >> 3 * 8);
	block->hdr.bitmap[4] |= (u8)(mask >> 4 * 8);
	block->hdr.bitmap[5] |= (u8)(mask >> 5 * 8);
	block->hdr.bitmap[6] |= (u8)(mask >> 6 * 8);
	block->hdr.bitmap[7] |= (u8)(mask >> 7 * 8);
}

/*
 * Clear a number of contiguous bits in the directory block bitmap.
 */
static void afs_clear_contig_bits(union afs_xdr_dir_block *block,
				  int bit, unsigned int nr_slots)
{
	u64 mask;

	mask = (1 << nr_slots) - 1;
	mask <<= bit;

	block->hdr.bitmap[0] &= ~(u8)(mask >> 0 * 8);
	block->hdr.bitmap[1] &= ~(u8)(mask >> 1 * 8);
	block->hdr.bitmap[2] &= ~(u8)(mask >> 2 * 8);
	block->hdr.bitmap[3] &= ~(u8)(mask >> 3 * 8);
	block->hdr.bitmap[4] &= ~(u8)(mask >> 4 * 8);
	block->hdr.bitmap[5] &= ~(u8)(mask >> 5 * 8);
	block->hdr.bitmap[6] &= ~(u8)(mask >> 6 * 8);
	block->hdr.bitmap[7] &= ~(u8)(mask >> 7 * 8);
}

/*
 * Get a specific block, extending the directory storage to cover it as needed.
 */
static union afs_xdr_dir_block *afs_dir_get_block(struct afs_dir_iter *iter, size_t block)
{
	struct folio_queue *fq;
	struct afs_vnode *dvnode = iter->dvnode;
	struct folio *folio;
	size_t blpos = block * AFS_DIR_BLOCK_SIZE;
	size_t blend = (block + 1) * AFS_DIR_BLOCK_SIZE, fpos = iter->fpos;
	int ret;

	if (dvnode->directory_size < blend) {
		size_t cur_size = dvnode->directory_size;

		ret = netfs_alloc_folioq_buffer(
			NULL, &dvnode->directory, &cur_size, blend,
			mapping_gfp_mask(dvnode->netfs.inode.i_mapping));
		dvnode->directory_size = cur_size;
		if (ret < 0)
			goto fail;
	}

	fq = iter->fq;
	if (!fq)
		fq = dvnode->directory;

	/* Search the folio queue for the folio containing the block... */
	for (; fq; fq = fq->next) {
		for (int s = iter->fq_slot; s < folioq_count(fq); s++) {
			size_t fsize = folioq_folio_size(fq, s);

			if (blend <= fpos + fsize) {
				/* ... and then return the mapped block. */
				folio = folioq_folio(fq, s);
				if (WARN_ON_ONCE(folio_pos(folio) != fpos))
					goto fail;
				iter->fq = fq;
				iter->fq_slot = s;
				iter->fpos = fpos;
				return kmap_local_folio(folio, blpos - fpos);
			}
			fpos += fsize;
		}
		iter->fq_slot = 0;
	}

fail:
	iter->fq = NULL;
	iter->fq_slot = 0;
	afs_invalidate_dir(dvnode, afs_dir_invalid_edit_get_block);
	return NULL;
}

/*
 * Scan a directory block looking for a dirent of the right name.
 */
static int afs_dir_scan_block(const union afs_xdr_dir_block *block, const struct qstr *name,
			      unsigned int blocknum)
{
	const union afs_xdr_dirent *de;
	u64 bitmap;
	int d, len, n;

	_enter("");

	bitmap  = (u64)block->hdr.bitmap[0] << 0 * 8;
	bitmap |= (u64)block->hdr.bitmap[1] << 1 * 8;
	bitmap |= (u64)block->hdr.bitmap[2] << 2 * 8;
	bitmap |= (u64)block->hdr.bitmap[3] << 3 * 8;
	bitmap |= (u64)block->hdr.bitmap[4] << 4 * 8;
	bitmap |= (u64)block->hdr.bitmap[5] << 5 * 8;
	bitmap |= (u64)block->hdr.bitmap[6] << 6 * 8;
	bitmap |= (u64)block->hdr.bitmap[7] << 7 * 8;

	for (d = (blocknum == 0 ? AFS_DIR_RESV_BLOCKS0 : AFS_DIR_RESV_BLOCKS);
	     d < AFS_DIR_SLOTS_PER_BLOCK;
	     d++) {
		if (!((bitmap >> d) & 1))
			continue;
		de = &block->dirents[d];
		if (de->u.valid != 1)
			continue;

		/* The block was NUL-terminated by afs_dir_check_page(). */
		len = strlen(de->u.name);
		if (len == name->len &&
		    memcmp(de->u.name, name->name, name->len) == 0)
			return d;

		n = round_up(12 + len + 1 + 4, AFS_DIR_DIRENT_SIZE);
		n /= AFS_DIR_DIRENT_SIZE;
		d += n - 1;
	}

	return -1;
}

/*
 * Initialise a new directory block.  Note that block 0 is special and contains
 * some extra metadata.
 */
static void afs_edit_init_block(union afs_xdr_dir_block *meta,
				union afs_xdr_dir_block *block, int block_num)
{
	memset(block, 0, sizeof(*block));
	block->hdr.npages = htons(1);
	block->hdr.magic = AFS_DIR_MAGIC;
	block->hdr.bitmap[0] = 1;

	if (block_num == 0) {
		block->hdr.bitmap[0] = 0xff;
		block->hdr.bitmap[1] = 0x1f;
		memset(block->meta.alloc_ctrs,
		       AFS_DIR_SLOTS_PER_BLOCK,
		       sizeof(block->meta.alloc_ctrs));
		meta->meta.alloc_ctrs[0] =
			AFS_DIR_SLOTS_PER_BLOCK - AFS_DIR_RESV_BLOCKS0;
	}

	if (block_num < AFS_DIR_BLOCKS_WITH_CTR)
		meta->meta.alloc_ctrs[block_num] =
			AFS_DIR_SLOTS_PER_BLOCK - AFS_DIR_RESV_BLOCKS;
}

/*
 * Edit a directory's file data to add a new directory entry.  Doing this after
 * create, mkdir, symlink, link or rename if the data version number is
 * incremented by exactly one avoids the need to re-download the entire
 * directory contents.
 *
 * The caller must hold the inode locked.
 */
void afs_edit_dir_add(struct afs_vnode *vnode,
		      struct qstr *name, struct afs_fid *new_fid,
		      enum afs_edit_dir_reason why)
{
	union afs_xdr_dir_block *meta, *block;
	union afs_xdr_dirent *de;
	struct afs_dir_iter iter = { .dvnode = vnode };
	unsigned int nr_blocks, b, entry;
	loff_t i_size;
	int slot;

	_enter(",,{%d,%s},", name->len, name->name);

	i_size = i_size_read(&vnode->netfs.inode);
	if (i_size > AFS_DIR_BLOCK_SIZE * AFS_DIR_MAX_BLOCKS ||
	    (i_size & (AFS_DIR_BLOCK_SIZE - 1))) {
		afs_invalidate_dir(vnode, afs_dir_invalid_edit_add_bad_size);
		return;
	}

	meta = afs_dir_get_block(&iter, 0);
	if (!meta)
		return;

	/* Work out how many slots we're going to need. */
	iter.nr_slots = afs_dir_calc_slots(name->len);

	if (i_size == 0)
		goto new_directory;
	nr_blocks = i_size / AFS_DIR_BLOCK_SIZE;

	/* Find a block that has sufficient slots available.  Each folio
	 * contains two or more directory blocks.
	 */
	for (b = 0; b < nr_blocks + 1; b++) {
		/* If the directory extended into a new folio, then we need to
		 * tack a new folio on the end.
		 */
		if (nr_blocks >= AFS_DIR_MAX_BLOCKS)
			goto error_too_many_blocks;

		/* Lower dir blocks have a counter in the header we can check. */
		if (b < AFS_DIR_BLOCKS_WITH_CTR &&
		    meta->meta.alloc_ctrs[b] < iter.nr_slots)
			continue;

		block = afs_dir_get_block(&iter, b);
		if (!block)
			goto error;

		/* Abandon the edit if we got a callback break. */
		if (!test_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
			goto already_invalidated;

		_debug("block %u: %2u %3u %u",
		       b,
		       (b < AFS_DIR_BLOCKS_WITH_CTR) ? meta->meta.alloc_ctrs[b] : 99,
		       ntohs(block->hdr.npages),
		       ntohs(block->hdr.magic));

		/* Initialise the block if necessary. */
		if (b == nr_blocks) {
			_debug("init %u", b);
			afs_edit_init_block(meta, block, b);
			afs_set_i_size(vnode, (b + 1) * AFS_DIR_BLOCK_SIZE);
		}

		/* We need to try and find one or more consecutive slots to
		 * hold the entry.
		 */
		slot = afs_find_contig_bits(block, iter.nr_slots);
		if (slot >= 0) {
			_debug("slot %u", slot);
			goto found_space;
		}

		kunmap_local(block);
	}

	/* There are no spare slots of sufficient size, yet the operation
	 * succeeded.  Download the directory again.
	 */
	trace_afs_edit_dir(vnode, why, afs_edit_dir_create_nospc, 0, 0, 0, 0, name->name);
	afs_invalidate_dir(vnode, afs_dir_invalid_edit_add_no_slots);
	goto out_unmap;

new_directory:
	afs_edit_init_block(meta, meta, 0);
	i_size = AFS_DIR_BLOCK_SIZE;
	afs_set_i_size(vnode, i_size);
	slot = AFS_DIR_RESV_BLOCKS0;
	block = afs_dir_get_block(&iter, 0);
	nr_blocks = 1;
	b = 0;

found_space:
	/* Set the dirent slot. */
	trace_afs_edit_dir(vnode, why, afs_edit_dir_create, b, slot,
			   new_fid->vnode, new_fid->unique, name->name);
	de = &block->dirents[slot];
	de->u.valid	= 1;
	de->u.unused[0]	= 0;
	de->u.hash_next	= 0; // TODO: Really need to maintain this
	de->u.vnode	= htonl(new_fid->vnode);
	de->u.unique	= htonl(new_fid->unique);
	memcpy(de->u.name, name->name, name->len + 1);
	de->u.name[name->len] = 0;

	/* Adjust the bitmap. */
	afs_set_contig_bits(block, slot, iter.nr_slots);

	/* Adjust the allocation counter. */
	if (b < AFS_DIR_BLOCKS_WITH_CTR)
		meta->meta.alloc_ctrs[b] -= iter.nr_slots;

	/* Adjust the hash chain. */
	entry = b * AFS_DIR_SLOTS_PER_BLOCK + slot;
	iter.bucket = afs_dir_hash_name(name);
	de->u.hash_next = meta->meta.hashtable[iter.bucket];
	meta->meta.hashtable[iter.bucket] = htons(entry);
	kunmap_local(block);

	inode_inc_iversion_raw(&vnode->netfs.inode);
	afs_stat_v(vnode, n_dir_cr);
	_debug("Insert %s in %u[%u]", name->name, b, slot);

	netfs_single_mark_inode_dirty(&vnode->netfs.inode);

out_unmap:
	kunmap_local(meta);
	_leave("");
	return;

already_invalidated:
	trace_afs_edit_dir(vnode, why, afs_edit_dir_create_inval, 0, 0, 0, 0, name->name);
	kunmap_local(block);
	goto out_unmap;

error_too_many_blocks:
	afs_invalidate_dir(vnode, afs_dir_invalid_edit_add_too_many_blocks);
error:
	trace_afs_edit_dir(vnode, why, afs_edit_dir_create_error, 0, 0, 0, 0, name->name);
	goto out_unmap;
}

/*
 * Edit a directory's file data to remove a new directory entry.  Doing this
 * after unlink, rmdir or rename if the data version number is incremented by
 * exactly one avoids the need to re-download the entire directory contents.
 *
 * The caller must hold the inode locked.
 */
void afs_edit_dir_remove(struct afs_vnode *vnode,
			 struct qstr *name, enum afs_edit_dir_reason why)
{
	union afs_xdr_dir_block *meta, *block, *pblock;
	union afs_xdr_dirent *de, *pde;
	struct afs_dir_iter iter = { .dvnode = vnode };
	struct afs_fid fid;
	unsigned int b, slot, entry;
	loff_t i_size;
	__be16 next;
	int found;

	_enter(",,{%d,%s},", name->len, name->name);

	i_size = i_size_read(&vnode->netfs.inode);
	if (i_size < AFS_DIR_BLOCK_SIZE ||
	    i_size > AFS_DIR_BLOCK_SIZE * AFS_DIR_MAX_BLOCKS ||
	    (i_size & (AFS_DIR_BLOCK_SIZE - 1))) {
		afs_invalidate_dir(vnode, afs_dir_invalid_edit_rem_bad_size);
		return;
	}

	if (!afs_dir_init_iter(&iter, name))
		return;

	meta = afs_dir_find_block(&iter, 0);
	if (!meta)
		return;

	/* Find the entry in the blob. */
	found = afs_dir_search_bucket(&iter, name, &fid);
	if (found < 0) {
		/* Didn't find the dirent to clobber.  Re-download. */
		trace_afs_edit_dir(vnode, why, afs_edit_dir_delete_noent,
				   0, 0, 0, 0, name->name);
		afs_invalidate_dir(vnode, afs_dir_invalid_edit_rem_wrong_name);
		goto out_unmap;
	}

	entry = found;
	b    = entry / AFS_DIR_SLOTS_PER_BLOCK;
	slot = entry % AFS_DIR_SLOTS_PER_BLOCK;

	block = afs_dir_find_block(&iter, b);
	if (!block)
		goto error;
	if (!test_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
		goto already_invalidated;

	/* Check and clear the entry. */
	de = &block->dirents[slot];
	if (de->u.valid != 1)
		goto error_unmap;

	trace_afs_edit_dir(vnode, why, afs_edit_dir_delete, b, slot,
			   ntohl(de->u.vnode), ntohl(de->u.unique),
			   name->name);

	/* Adjust the bitmap. */
	afs_clear_contig_bits(block, slot, iter.nr_slots);

	/* Adjust the allocation counter. */
	if (b < AFS_DIR_BLOCKS_WITH_CTR)
		meta->meta.alloc_ctrs[b] += iter.nr_slots;

	/* Clear the constituent entries. */
	next = de->u.hash_next;
	memset(de, 0, sizeof(*de) * iter.nr_slots);
	kunmap_local(block);

	/* Adjust the hash chain: if iter->prev_entry is 0, the hashtable head
	 * index is previous; otherwise it's slot number of the previous entry.
	 */
	if (!iter.prev_entry) {
		__be16 prev_next = meta->meta.hashtable[iter.bucket];

		if (unlikely(prev_next != htons(entry))) {
			pr_warn("%llx:%llx:%x: not head of chain b=%x p=%x,%x e=%x %*s",
				vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique,
				iter.bucket, iter.prev_entry, prev_next, entry,
				name->len, name->name);
			goto error;
		}
		meta->meta.hashtable[iter.bucket] = next;
	} else {
		unsigned int pb = iter.prev_entry / AFS_DIR_SLOTS_PER_BLOCK;
		unsigned int ps = iter.prev_entry % AFS_DIR_SLOTS_PER_BLOCK;
		__be16 prev_next;

		pblock = afs_dir_find_block(&iter, pb);
		if (!pblock)
			goto error;
		pde = &pblock->dirents[ps];
		prev_next = pde->u.hash_next;
		if (prev_next != htons(entry)) {
			kunmap_local(pblock);
			pr_warn("%llx:%llx:%x: not prev in chain b=%x p=%x,%x e=%x %*s",
				vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique,
				iter.bucket, iter.prev_entry, prev_next, entry,
				name->len, name->name);
			goto error;
		}
		pde->u.hash_next = next;
		kunmap_local(pblock);
	}

	netfs_single_mark_inode_dirty(&vnode->netfs.inode);

	inode_set_iversion_raw(&vnode->netfs.inode, vnode->status.data_version);
	afs_stat_v(vnode, n_dir_rm);
	_debug("Remove %s from %u[%u]", name->name, b, slot);

out_unmap:
	kunmap_local(meta);
	_leave("");
	return;

already_invalidated:
	kunmap_local(block);
	trace_afs_edit_dir(vnode, why, afs_edit_dir_delete_inval,
			   0, 0, 0, 0, name->name);
	goto out_unmap;

error_unmap:
	kunmap_local(block);
error:
	trace_afs_edit_dir(vnode, why, afs_edit_dir_delete_error,
			   0, 0, 0, 0, name->name);
	goto out_unmap;
}

/*
 * Edit a subdirectory that has been moved between directories to update the
 * ".." entry.
 */
void afs_edit_dir_update_dotdot(struct afs_vnode *vnode, struct afs_vnode *new_dvnode,
				enum afs_edit_dir_reason why)
{
	union afs_xdr_dir_block *block;
	union afs_xdr_dirent *de;
	struct afs_dir_iter iter = { .dvnode = vnode };
	unsigned int nr_blocks, b;
	loff_t i_size;
	int slot;

	_enter("");

	i_size = i_size_read(&vnode->netfs.inode);
	if (i_size < AFS_DIR_BLOCK_SIZE) {
		afs_invalidate_dir(vnode, afs_dir_invalid_edit_upd_bad_size);
		return;
	}

	nr_blocks = i_size / AFS_DIR_BLOCK_SIZE;

	/* Find a block that has sufficient slots available.  Each folio
	 * contains two or more directory blocks.
	 */
	for (b = 0; b < nr_blocks; b++) {
		block = afs_dir_get_block(&iter, b);
		if (!block)
			goto error;

		/* Abandon the edit if we got a callback break. */
		if (!test_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
			goto already_invalidated;

		slot = afs_dir_scan_block(block, &dotdot_name, b);
		if (slot >= 0)
			goto found_dirent;

		kunmap_local(block);
	}

	/* Didn't find the dirent to clobber.  Download the directory again. */
	trace_afs_edit_dir(vnode, why, afs_edit_dir_update_nodd,
			   0, 0, 0, 0, "..");
	afs_invalidate_dir(vnode, afs_dir_invalid_edit_upd_no_dd);
	goto out;

found_dirent:
	de = &block->dirents[slot];
	de->u.vnode  = htonl(new_dvnode->fid.vnode);
	de->u.unique = htonl(new_dvnode->fid.unique);

	trace_afs_edit_dir(vnode, why, afs_edit_dir_update_dd, b, slot,
			   ntohl(de->u.vnode), ntohl(de->u.unique), "..");

	kunmap_local(block);
	netfs_single_mark_inode_dirty(&vnode->netfs.inode);
	inode_set_iversion_raw(&vnode->netfs.inode, vnode->status.data_version);

out:
	_leave("");
	return;

already_invalidated:
	kunmap_local(block);
	trace_afs_edit_dir(vnode, why, afs_edit_dir_update_inval,
			   0, 0, 0, 0, "..");
	goto out;

error:
	trace_afs_edit_dir(vnode, why, afs_edit_dir_update_error,
			   0, 0, 0, 0, "..");
	goto out;
}

/*
 * Initialise a new directory.  We need to fill in the "." and ".." entries.
 */
void afs_mkdir_init_dir(struct afs_vnode *dvnode, struct afs_vnode *parent_dvnode)
{
	union afs_xdr_dir_block *meta;
	struct afs_dir_iter iter = { .dvnode = dvnode };
	union afs_xdr_dirent *de;
	unsigned int slot = AFS_DIR_RESV_BLOCKS0;
	loff_t i_size;

	i_size = i_size_read(&dvnode->netfs.inode);
	if (i_size != AFS_DIR_BLOCK_SIZE) {
		afs_invalidate_dir(dvnode, afs_dir_invalid_edit_add_bad_size);
		return;
	}

	meta = afs_dir_get_block(&iter, 0);
	if (!meta)
		return;

	afs_edit_init_block(meta, meta, 0);

	de = &meta->dirents[slot];
	de->u.valid  = 1;
	de->u.vnode  = htonl(dvnode->fid.vnode);
	de->u.unique = htonl(dvnode->fid.unique);
	memcpy(de->u.name, ".", 2);
	trace_afs_edit_dir(dvnode, afs_edit_dir_for_mkdir, afs_edit_dir_mkdir, 0, slot,
			   dvnode->fid.vnode, dvnode->fid.unique, ".");
	slot++;

	de = &meta->dirents[slot];
	de->u.valid  = 1;
	de->u.vnode  = htonl(parent_dvnode->fid.vnode);
	de->u.unique = htonl(parent_dvnode->fid.unique);
	memcpy(de->u.name, "..", 3);
	trace_afs_edit_dir(dvnode, afs_edit_dir_for_mkdir, afs_edit_dir_mkdir, 0, slot,
			   parent_dvnode->fid.vnode, parent_dvnode->fid.unique, "..");

	afs_set_contig_bits(meta, AFS_DIR_RESV_BLOCKS0, 2);
	meta->meta.alloc_ctrs[0] -= 2;
	kunmap_local(meta);

	netfs_single_mark_inode_dirty(&dvnode->netfs.inode);
	set_bit(AFS_VNODE_DIR_VALID, &dvnode->flags);
	set_bit(AFS_VNODE_DIR_READ, &dvnode->flags);
}
