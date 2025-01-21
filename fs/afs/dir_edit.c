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
 * Get a new directory folio.
 */
static struct folio *afs_dir_get_folio(struct afs_vnode *vnode, pgoff_t index)
{
	struct address_space *mapping = vnode->netfs.inode.i_mapping;
	struct folio *folio;

	folio = __filemap_get_folio(mapping, index,
				    FGP_LOCK | FGP_ACCESSED | FGP_CREAT,
				    mapping->gfp_mask);
	if (IS_ERR(folio)) {
		clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
		return NULL;
	}
	if (!folio_test_private(folio))
		folio_attach_private(folio, (void *)1);
	return folio;
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
	struct folio *folio0, *folio;
	unsigned int need_slots, nr_blocks, b;
	pgoff_t index;
	loff_t i_size;
	int slot;

	_enter(",,{%d,%s},", name->len, name->name);

	i_size = i_size_read(&vnode->netfs.inode);
	if (i_size > AFS_DIR_BLOCK_SIZE * AFS_DIR_MAX_BLOCKS ||
	    (i_size & (AFS_DIR_BLOCK_SIZE - 1))) {
		clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
		return;
	}

	folio0 = afs_dir_get_folio(vnode, 0);
	if (!folio0) {
		_leave(" [fgp]");
		return;
	}

	/* Work out how many slots we're going to need. */
	need_slots = afs_dir_calc_slots(name->len);

	meta = kmap_local_folio(folio0, 0);
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
		index = b / AFS_DIR_BLOCKS_PER_PAGE;
		if (nr_blocks >= AFS_DIR_MAX_BLOCKS)
			goto error;
		if (index >= folio_nr_pages(folio0)) {
			folio = afs_dir_get_folio(vnode, index);
			if (!folio)
				goto error;
		} else {
			folio = folio0;
		}

		block = kmap_local_folio(folio, b * AFS_DIR_BLOCK_SIZE - folio_pos(folio));

		/* Abandon the edit if we got a callback break. */
		if (!test_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
			goto invalidated;

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

		/* Only lower dir blocks have a counter in the header. */
		if (b >= AFS_DIR_BLOCKS_WITH_CTR ||
		    meta->meta.alloc_ctrs[b] >= need_slots) {
			/* We need to try and find one or more consecutive
			 * slots to hold the entry.
			 */
			slot = afs_find_contig_bits(block, need_slots);
			if (slot >= 0) {
				_debug("slot %u", slot);
				goto found_space;
			}
		}

		kunmap_local(block);
		if (folio != folio0) {
			folio_unlock(folio);
			folio_put(folio);
		}
	}

	/* There are no spare slots of sufficient size, yet the operation
	 * succeeded.  Download the directory again.
	 */
	trace_afs_edit_dir(vnode, why, afs_edit_dir_create_nospc, 0, 0, 0, 0, name->name);
	clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
	goto out_unmap;

new_directory:
	afs_edit_init_block(meta, meta, 0);
	i_size = AFS_DIR_BLOCK_SIZE;
	afs_set_i_size(vnode, i_size);
	slot = AFS_DIR_RESV_BLOCKS0;
	folio = folio0;
	block = kmap_local_folio(folio, 0);
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
	afs_set_contig_bits(block, slot, need_slots);
	kunmap_local(block);
	if (folio != folio0) {
		folio_unlock(folio);
		folio_put(folio);
	}

	/* Adjust the allocation counter. */
	if (b < AFS_DIR_BLOCKS_WITH_CTR)
		meta->meta.alloc_ctrs[b] -= need_slots;

	inode_inc_iversion_raw(&vnode->netfs.inode);
	afs_stat_v(vnode, n_dir_cr);
	_debug("Insert %s in %u[%u]", name->name, b, slot);

out_unmap:
	kunmap_local(meta);
	folio_unlock(folio0);
	folio_put(folio0);
	_leave("");
	return;

invalidated:
	trace_afs_edit_dir(vnode, why, afs_edit_dir_create_inval, 0, 0, 0, 0, name->name);
	clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
	kunmap_local(block);
	if (folio != folio0) {
		folio_unlock(folio);
		folio_put(folio);
	}
	goto out_unmap;

error:
	trace_afs_edit_dir(vnode, why, afs_edit_dir_create_error, 0, 0, 0, 0, name->name);
	clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
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
	union afs_xdr_dir_block *meta, *block;
	union afs_xdr_dirent *de;
	struct folio *folio0, *folio;
	unsigned int need_slots, nr_blocks, b;
	pgoff_t index;
	loff_t i_size;
	int slot;

	_enter(",,{%d,%s},", name->len, name->name);

	i_size = i_size_read(&vnode->netfs.inode);
	if (i_size < AFS_DIR_BLOCK_SIZE ||
	    i_size > AFS_DIR_BLOCK_SIZE * AFS_DIR_MAX_BLOCKS ||
	    (i_size & (AFS_DIR_BLOCK_SIZE - 1))) {
		clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
		return;
	}
	nr_blocks = i_size / AFS_DIR_BLOCK_SIZE;

	folio0 = afs_dir_get_folio(vnode, 0);
	if (!folio0) {
		_leave(" [fgp]");
		return;
	}

	/* Work out how many slots we're going to discard. */
	need_slots = afs_dir_calc_slots(name->len);

	meta = kmap_local_folio(folio0, 0);

	/* Find a block that has sufficient slots available.  Each folio
	 * contains two or more directory blocks.
	 */
	for (b = 0; b < nr_blocks; b++) {
		index = b / AFS_DIR_BLOCKS_PER_PAGE;
		if (index >= folio_nr_pages(folio0)) {
			folio = afs_dir_get_folio(vnode, index);
			if (!folio)
				goto error;
		} else {
			folio = folio0;
		}

		block = kmap_local_folio(folio, b * AFS_DIR_BLOCK_SIZE - folio_pos(folio));

		/* Abandon the edit if we got a callback break. */
		if (!test_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
			goto invalidated;

		if (b > AFS_DIR_BLOCKS_WITH_CTR ||
		    meta->meta.alloc_ctrs[b] <= AFS_DIR_SLOTS_PER_BLOCK - 1 - need_slots) {
			slot = afs_dir_scan_block(block, name, b);
			if (slot >= 0)
				goto found_dirent;
		}

		kunmap_local(block);
		if (folio != folio0) {
			folio_unlock(folio);
			folio_put(folio);
		}
	}

	/* Didn't find the dirent to clobber.  Download the directory again. */
	trace_afs_edit_dir(vnode, why, afs_edit_dir_delete_noent,
			   0, 0, 0, 0, name->name);
	clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
	goto out_unmap;

found_dirent:
	de = &block->dirents[slot];

	trace_afs_edit_dir(vnode, why, afs_edit_dir_delete, b, slot,
			   ntohl(de->u.vnode), ntohl(de->u.unique),
			   name->name);

	memset(de, 0, sizeof(*de) * need_slots);

	/* Adjust the bitmap. */
	afs_clear_contig_bits(block, slot, need_slots);
	kunmap_local(block);
	if (folio != folio0) {
		folio_unlock(folio);
		folio_put(folio);
	}

	/* Adjust the allocation counter. */
	if (b < AFS_DIR_BLOCKS_WITH_CTR)
		meta->meta.alloc_ctrs[b] += need_slots;

	inode_set_iversion_raw(&vnode->netfs.inode, vnode->status.data_version);
	afs_stat_v(vnode, n_dir_rm);
	_debug("Remove %s from %u[%u]", name->name, b, slot);

out_unmap:
	kunmap_local(meta);
	folio_unlock(folio0);
	folio_put(folio0);
	_leave("");
	return;

invalidated:
	trace_afs_edit_dir(vnode, why, afs_edit_dir_delete_inval,
			   0, 0, 0, 0, name->name);
	clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
	kunmap_local(block);
	if (folio != folio0) {
		folio_unlock(folio);
		folio_put(folio);
	}
	goto out_unmap;

error:
	trace_afs_edit_dir(vnode, why, afs_edit_dir_delete_error,
			   0, 0, 0, 0, name->name);
	clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
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
	struct folio *folio;
	unsigned int nr_blocks, b;
	pgoff_t index;
	loff_t i_size;
	int slot;

	_enter("");

	i_size = i_size_read(&vnode->netfs.inode);
	if (i_size < AFS_DIR_BLOCK_SIZE) {
		clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
		return;
	}
	nr_blocks = i_size / AFS_DIR_BLOCK_SIZE;

	/* Find a block that has sufficient slots available.  Each folio
	 * contains two or more directory blocks.
	 */
	for (b = 0; b < nr_blocks; b++) {
		index = b / AFS_DIR_BLOCKS_PER_PAGE;
		folio = afs_dir_get_folio(vnode, index);
		if (!folio)
			goto error;

		block = kmap_local_folio(folio, b * AFS_DIR_BLOCK_SIZE - folio_pos(folio));

		/* Abandon the edit if we got a callback break. */
		if (!test_bit(AFS_VNODE_DIR_VALID, &vnode->flags))
			goto invalidated;

		slot = afs_dir_scan_block(block, &dotdot_name, b);
		if (slot >= 0)
			goto found_dirent;

		kunmap_local(block);
		folio_unlock(folio);
		folio_put(folio);
	}

	/* Didn't find the dirent to clobber.  Download the directory again. */
	trace_afs_edit_dir(vnode, why, afs_edit_dir_update_nodd,
			   0, 0, 0, 0, "..");
	clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
	goto out;

found_dirent:
	de = &block->dirents[slot];
	de->u.vnode  = htonl(new_dvnode->fid.vnode);
	de->u.unique = htonl(new_dvnode->fid.unique);

	trace_afs_edit_dir(vnode, why, afs_edit_dir_update_dd, b, slot,
			   ntohl(de->u.vnode), ntohl(de->u.unique), "..");

	kunmap_local(block);
	folio_unlock(folio);
	folio_put(folio);
	inode_set_iversion_raw(&vnode->netfs.inode, vnode->status.data_version);

out:
	_leave("");
	return;

invalidated:
	kunmap_local(block);
	folio_unlock(folio);
	folio_put(folio);
	trace_afs_edit_dir(vnode, why, afs_edit_dir_update_inval,
			   0, 0, 0, 0, "..");
	clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
	goto out;

error:
	trace_afs_edit_dir(vnode, why, afs_edit_dir_update_error,
			   0, 0, 0, 0, "..");
	clear_bit(AFS_VNODE_DIR_VALID, &vnode->flags);
	goto out;
}
