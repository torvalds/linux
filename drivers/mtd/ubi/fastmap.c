/*
 * Copyright (c) 2012 Linutronix GmbH
 * Author: Richard Weinberger <richard@nod.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 */

#include <linux/crc32.h>
#include "ubi.h"

/**
 * ubi_calc_fm_size - calculates the fastmap size in bytes for an UBI device.
 * @ubi: UBI device description object
 */
size_t ubi_calc_fm_size(struct ubi_device *ubi)
{
	size_t size;

	size = sizeof(struct ubi_fm_sb) + \
		sizeof(struct ubi_fm_hdr) + \
		sizeof(struct ubi_fm_scan_pool) + \
		sizeof(struct ubi_fm_scan_pool) + \
		(ubi->peb_count * sizeof(struct ubi_fm_ec)) + \
		(sizeof(struct ubi_fm_eba) + \
		(ubi->peb_count * sizeof(__be32))) + \
		sizeof(struct ubi_fm_volhdr) * UBI_MAX_VOLUMES;
	return roundup(size, ubi->leb_size);
}


/**
 * new_fm_vhdr - allocate a new volume header for fastmap usage.
 * @ubi: UBI device description object
 * @vol_id: the VID of the new header
 *
 * Returns a new struct ubi_vid_hdr on success.
 * NULL indicates out of memory.
 */
static struct ubi_vid_hdr *new_fm_vhdr(struct ubi_device *ubi, int vol_id)
{
	struct ubi_vid_hdr *new;

	new = ubi_zalloc_vid_hdr(ubi, GFP_KERNEL);
	if (!new)
		goto out;

	new->vol_type = UBI_VID_DYNAMIC;
	new->vol_id = cpu_to_be32(vol_id);

	/* UBI implementations without fastmap support have to delete the
	 * fastmap.
	 */
	new->compat = UBI_COMPAT_DELETE;

out:
	return new;
}

/**
 * add_aeb - create and add a attach erase block to a given list.
 * @ai: UBI attach info object
 * @list: the target list
 * @pnum: PEB number of the new attach erase block
 * @ec: erease counter of the new LEB
 * @scrub: scrub this PEB after attaching
 *
 * Returns 0 on success, < 0 indicates an internal error.
 */
static int add_aeb(struct ubi_attach_info *ai, struct list_head *list,
		   int pnum, int ec, int scrub)
{
	struct ubi_ainf_peb *aeb;

	aeb = kmem_cache_alloc(ai->aeb_slab_cache, GFP_KERNEL);
	if (!aeb)
		return -ENOMEM;

	aeb->pnum = pnum;
	aeb->ec = ec;
	aeb->lnum = -1;
	aeb->scrub = scrub;
	aeb->copy_flag = aeb->sqnum = 0;

	ai->ec_sum += aeb->ec;
	ai->ec_count++;

	if (ai->max_ec < aeb->ec)
		ai->max_ec = aeb->ec;

	if (ai->min_ec > aeb->ec)
		ai->min_ec = aeb->ec;

	list_add_tail(&aeb->u.list, list);

	return 0;
}

/**
 * add_vol - create and add a new volume to ubi_attach_info.
 * @ai: ubi_attach_info object
 * @vol_id: VID of the new volume
 * @used_ebs: number of used EBS
 * @data_pad: data padding value of the new volume
 * @vol_type: volume type
 * @last_eb_bytes: number of bytes in the last LEB
 *
 * Returns the new struct ubi_ainf_volume on success.
 * NULL indicates an error.
 */
static struct ubi_ainf_volume *add_vol(struct ubi_attach_info *ai, int vol_id,
				       int used_ebs, int data_pad, u8 vol_type,
				       int last_eb_bytes)
{
	struct ubi_ainf_volume *av;
	struct rb_node **p = &ai->volumes.rb_node, *parent = NULL;

	while (*p) {
		parent = *p;
		av = rb_entry(parent, struct ubi_ainf_volume, rb);

		if (vol_id > av->vol_id)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	av = kmalloc(sizeof(struct ubi_ainf_volume), GFP_KERNEL);
	if (!av)
		goto out;

	av->highest_lnum = av->leb_count = 0;
	av->vol_id = vol_id;
	av->used_ebs = used_ebs;
	av->data_pad = data_pad;
	av->last_data_size = last_eb_bytes;
	av->compat = 0;
	av->vol_type = vol_type;
	av->root = RB_ROOT;

	dbg_bld("found volume (ID %i)", vol_id);

	rb_link_node(&av->rb, parent, p);
	rb_insert_color(&av->rb, &ai->volumes);

out:
	return av;
}

/**
 * assign_aeb_to_av - assigns a SEB to a given ainf_volume and removes it
 * from it's original list.
 * @ai: ubi_attach_info object
 * @aeb: the to be assigned SEB
 * @av: target scan volume
 */
static void assign_aeb_to_av(struct ubi_attach_info *ai,
			     struct ubi_ainf_peb *aeb,
			     struct ubi_ainf_volume *av)
{
	struct ubi_ainf_peb *tmp_aeb;
	struct rb_node **p = &ai->volumes.rb_node, *parent = NULL;

	p = &av->root.rb_node;
	while (*p) {
		parent = *p;

		tmp_aeb = rb_entry(parent, struct ubi_ainf_peb, u.rb);
		if (aeb->lnum != tmp_aeb->lnum) {
			if (aeb->lnum < tmp_aeb->lnum)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;

			continue;
		} else
			break;
	}

	list_del(&aeb->u.list);
	av->leb_count++;

	rb_link_node(&aeb->u.rb, parent, p);
	rb_insert_color(&aeb->u.rb, &av->root);
}

/**
 * update_vol - inserts or updates a LEB which was found a pool.
 * @ubi: the UBI device object
 * @ai: attach info object
 * @av: the volume this LEB belongs to
 * @new_vh: the volume header derived from new_aeb
 * @new_aeb: the AEB to be examined
 *
 * Returns 0 on success, < 0 indicates an internal error.
 */
static int update_vol(struct ubi_device *ubi, struct ubi_attach_info *ai,
		      struct ubi_ainf_volume *av, struct ubi_vid_hdr *new_vh,
		      struct ubi_ainf_peb *new_aeb)
{
	struct rb_node **p = &av->root.rb_node, *parent = NULL;
	struct ubi_ainf_peb *aeb, *victim;
	int cmp_res;

	while (*p) {
		parent = *p;
		aeb = rb_entry(parent, struct ubi_ainf_peb, u.rb);

		if (be32_to_cpu(new_vh->lnum) != aeb->lnum) {
			if (be32_to_cpu(new_vh->lnum) < aeb->lnum)
				p = &(*p)->rb_left;
			else
				p = &(*p)->rb_right;

			continue;
		}

		/* This case can happen if the fastmap gets written
		 * because of a volume change (creation, deletion, ..).
		 * Then a PEB can be within the persistent EBA and the pool.
		 */
		if (aeb->pnum == new_aeb->pnum) {
			ubi_assert(aeb->lnum == new_aeb->lnum);
			kmem_cache_free(ai->aeb_slab_cache, new_aeb);

			return 0;
		}

		cmp_res = ubi_compare_lebs(ubi, aeb, new_aeb->pnum, new_vh);
		if (cmp_res < 0)
			return cmp_res;

		/* new_aeb is newer */
		if (cmp_res & 1) {
			victim = kmem_cache_alloc(ai->aeb_slab_cache,
				GFP_KERNEL);
			if (!victim)
				return -ENOMEM;

			victim->ec = aeb->ec;
			victim->pnum = aeb->pnum;
			list_add_tail(&victim->u.list, &ai->erase);

			if (av->highest_lnum == be32_to_cpu(new_vh->lnum))
				av->last_data_size = \
					be32_to_cpu(new_vh->data_size);

			dbg_bld("vol %i: AEB %i's PEB %i is the newer",
				av->vol_id, aeb->lnum, new_aeb->pnum);

			aeb->ec = new_aeb->ec;
			aeb->pnum = new_aeb->pnum;
			aeb->copy_flag = new_vh->copy_flag;
			aeb->scrub = new_aeb->scrub;
			kmem_cache_free(ai->aeb_slab_cache, new_aeb);

		/* new_aeb is older */
		} else {
			dbg_bld("vol %i: AEB %i's PEB %i is old, dropping it",
				av->vol_id, aeb->lnum, new_aeb->pnum);
			list_add_tail(&new_aeb->u.list, &ai->erase);
		}

		return 0;
	}
	/* This LEB is new, let's add it to the volume */

	if (av->highest_lnum <= be32_to_cpu(new_vh->lnum)) {
		av->highest_lnum = be32_to_cpu(new_vh->lnum);
		av->last_data_size = be32_to_cpu(new_vh->data_size);
	}

	if (av->vol_type == UBI_STATIC_VOLUME)
		av->used_ebs = be32_to_cpu(new_vh->used_ebs);

	av->leb_count++;

	rb_link_node(&new_aeb->u.rb, parent, p);
	rb_insert_color(&new_aeb->u.rb, &av->root);

	return 0;
}

/**
 * process_pool_aeb - we found a non-empty PEB in a pool.
 * @ubi: UBI device object
 * @ai: attach info object
 * @new_vh: the volume header derived from new_aeb
 * @new_aeb: the AEB to be examined
 *
 * Returns 0 on success, < 0 indicates an internal error.
 */
static int process_pool_aeb(struct ubi_device *ubi, struct ubi_attach_info *ai,
			    struct ubi_vid_hdr *new_vh,
			    struct ubi_ainf_peb *new_aeb)
{
	struct ubi_ainf_volume *av, *tmp_av = NULL;
	struct rb_node **p = &ai->volumes.rb_node, *parent = NULL;
	int found = 0;

	if (be32_to_cpu(new_vh->vol_id) == UBI_FM_SB_VOLUME_ID ||
		be32_to_cpu(new_vh->vol_id) == UBI_FM_DATA_VOLUME_ID) {
		kmem_cache_free(ai->aeb_slab_cache, new_aeb);

		return 0;
	}

	/* Find the volume this SEB belongs to */
	while (*p) {
		parent = *p;
		tmp_av = rb_entry(parent, struct ubi_ainf_volume, rb);

		if (be32_to_cpu(new_vh->vol_id) > tmp_av->vol_id)
			p = &(*p)->rb_left;
		else if (be32_to_cpu(new_vh->vol_id) < tmp_av->vol_id)
			p = &(*p)->rb_right;
		else {
			found = 1;
			break;
		}
	}

	if (found)
		av = tmp_av;
	else {
		ubi_err(ubi, "orphaned volume in fastmap pool!");
		kmem_cache_free(ai->aeb_slab_cache, new_aeb);
		return UBI_BAD_FASTMAP;
	}

	ubi_assert(be32_to_cpu(new_vh->vol_id) == av->vol_id);

	return update_vol(ubi, ai, av, new_vh, new_aeb);
}

/**
 * unmap_peb - unmap a PEB.
 * If fastmap detects a free PEB in the pool it has to check whether
 * this PEB has been unmapped after writing the fastmap.
 *
 * @ai: UBI attach info object
 * @pnum: The PEB to be unmapped
 */
static void unmap_peb(struct ubi_attach_info *ai, int pnum)
{
	struct ubi_ainf_volume *av;
	struct rb_node *node, *node2;
	struct ubi_ainf_peb *aeb;

	for (node = rb_first(&ai->volumes); node; node = rb_next(node)) {
		av = rb_entry(node, struct ubi_ainf_volume, rb);

		for (node2 = rb_first(&av->root); node2;
		     node2 = rb_next(node2)) {
			aeb = rb_entry(node2, struct ubi_ainf_peb, u.rb);
			if (aeb->pnum == pnum) {
				rb_erase(&aeb->u.rb, &av->root);
				kmem_cache_free(ai->aeb_slab_cache, aeb);
				return;
			}
		}
	}
}

/**
 * scan_pool - scans a pool for changed (no longer empty PEBs).
 * @ubi: UBI device object
 * @ai: attach info object
 * @pebs: an array of all PEB numbers in the to be scanned pool
 * @pool_size: size of the pool (number of entries in @pebs)
 * @max_sqnum: pointer to the maximal sequence number
 * @eba_orphans: list of PEBs which need to be scanned
 * @free: list of PEBs which are most likely free (and go into @ai->free)
 *
 * Returns 0 on success, if the pool is unusable UBI_BAD_FASTMAP is returned.
 * < 0 indicates an internal error.
 */
static int scan_pool(struct ubi_device *ubi, struct ubi_attach_info *ai,
		     int *pebs, int pool_size, unsigned long long *max_sqnum,
		     struct list_head *eba_orphans, struct list_head *free)
{
	struct ubi_vid_hdr *vh;
	struct ubi_ec_hdr *ech;
	struct ubi_ainf_peb *new_aeb, *tmp_aeb;
	int i, pnum, err, found_orphan, ret = 0;

	ech = kzalloc(ubi->ec_hdr_alsize, GFP_KERNEL);
	if (!ech)
		return -ENOMEM;

	vh = ubi_zalloc_vid_hdr(ubi, GFP_KERNEL);
	if (!vh) {
		kfree(ech);
		return -ENOMEM;
	}

	dbg_bld("scanning fastmap pool: size = %i", pool_size);

	/*
	 * Now scan all PEBs in the pool to find changes which have been made
	 * after the creation of the fastmap
	 */
	for (i = 0; i < pool_size; i++) {
		int scrub = 0;
		int image_seq;

		pnum = be32_to_cpu(pebs[i]);

		if (ubi_io_is_bad(ubi, pnum)) {
			ubi_err(ubi, "bad PEB in fastmap pool!");
			ret = UBI_BAD_FASTMAP;
			goto out;
		}

		err = ubi_io_read_ec_hdr(ubi, pnum, ech, 0);
		if (err && err != UBI_IO_BITFLIPS) {
			ubi_err(ubi, "unable to read EC header! PEB:%i err:%i",
				pnum, err);
			ret = err > 0 ? UBI_BAD_FASTMAP : err;
			goto out;
		} else if (err == UBI_IO_BITFLIPS)
			scrub = 1;

		/*
		 * Older UBI implementations have image_seq set to zero, so
		 * we shouldn't fail if image_seq == 0.
		 */
		image_seq = be32_to_cpu(ech->image_seq);

		if (image_seq && (image_seq != ubi->image_seq)) {
			ubi_err(ubi, "bad image seq: 0x%x, expected: 0x%x",
				be32_to_cpu(ech->image_seq), ubi->image_seq);
			ret = UBI_BAD_FASTMAP;
			goto out;
		}

		err = ubi_io_read_vid_hdr(ubi, pnum, vh, 0);
		if (err == UBI_IO_FF || err == UBI_IO_FF_BITFLIPS) {
			unsigned long long ec = be64_to_cpu(ech->ec);
			unmap_peb(ai, pnum);
			dbg_bld("Adding PEB to free: %i", pnum);
			if (err == UBI_IO_FF_BITFLIPS)
				add_aeb(ai, free, pnum, ec, 1);
			else
				add_aeb(ai, free, pnum, ec, 0);
			continue;
		} else if (err == 0 || err == UBI_IO_BITFLIPS) {
			dbg_bld("Found non empty PEB:%i in pool", pnum);

			if (err == UBI_IO_BITFLIPS)
				scrub = 1;

			found_orphan = 0;
			list_for_each_entry(tmp_aeb, eba_orphans, u.list) {
				if (tmp_aeb->pnum == pnum) {
					found_orphan = 1;
					break;
				}
			}
			if (found_orphan) {
				list_del(&tmp_aeb->u.list);
				kmem_cache_free(ai->aeb_slab_cache, tmp_aeb);
			}

			new_aeb = kmem_cache_alloc(ai->aeb_slab_cache,
						   GFP_KERNEL);
			if (!new_aeb) {
				ret = -ENOMEM;
				goto out;
			}

			new_aeb->ec = be64_to_cpu(ech->ec);
			new_aeb->pnum = pnum;
			new_aeb->lnum = be32_to_cpu(vh->lnum);
			new_aeb->sqnum = be64_to_cpu(vh->sqnum);
			new_aeb->copy_flag = vh->copy_flag;
			new_aeb->scrub = scrub;

			if (*max_sqnum < new_aeb->sqnum)
				*max_sqnum = new_aeb->sqnum;

			err = process_pool_aeb(ubi, ai, vh, new_aeb);
			if (err) {
				ret = err > 0 ? UBI_BAD_FASTMAP : err;
				goto out;
			}
		} else {
			/* We are paranoid and fall back to scanning mode */
			ubi_err(ubi, "fastmap pool PEBs contains damaged PEBs!");
			ret = err > 0 ? UBI_BAD_FASTMAP : err;
			goto out;
		}

	}

out:
	ubi_free_vid_hdr(ubi, vh);
	kfree(ech);
	return ret;
}

/**
 * count_fastmap_pebs - Counts the PEBs found by fastmap.
 * @ai: The UBI attach info object
 */
static int count_fastmap_pebs(struct ubi_attach_info *ai)
{
	struct ubi_ainf_peb *aeb;
	struct ubi_ainf_volume *av;
	struct rb_node *rb1, *rb2;
	int n = 0;

	list_for_each_entry(aeb, &ai->erase, u.list)
		n++;

	list_for_each_entry(aeb, &ai->free, u.list)
		n++;

	 ubi_rb_for_each_entry(rb1, av, &ai->volumes, rb)
		ubi_rb_for_each_entry(rb2, aeb, &av->root, u.rb)
			n++;

	return n;
}

/**
 * ubi_attach_fastmap - creates ubi_attach_info from a fastmap.
 * @ubi: UBI device object
 * @ai: UBI attach info object
 * @fm: the fastmap to be attached
 *
 * Returns 0 on success, UBI_BAD_FASTMAP if the found fastmap was unusable.
 * < 0 indicates an internal error.
 */
static int ubi_attach_fastmap(struct ubi_device *ubi,
			      struct ubi_attach_info *ai,
			      struct ubi_fastmap_layout *fm)
{
	struct list_head used, eba_orphans, free;
	struct ubi_ainf_volume *av;
	struct ubi_ainf_peb *aeb, *tmp_aeb, *_tmp_aeb;
	struct ubi_ec_hdr *ech;
	struct ubi_fm_sb *fmsb;
	struct ubi_fm_hdr *fmhdr;
	struct ubi_fm_scan_pool *fmpl1, *fmpl2;
	struct ubi_fm_ec *fmec;
	struct ubi_fm_volhdr *fmvhdr;
	struct ubi_fm_eba *fm_eba;
	int ret, i, j, pool_size, wl_pool_size;
	size_t fm_pos = 0, fm_size = ubi->fm_size;
	unsigned long long max_sqnum = 0;
	void *fm_raw = ubi->fm_buf;

	INIT_LIST_HEAD(&used);
	INIT_LIST_HEAD(&free);
	INIT_LIST_HEAD(&eba_orphans);
	INIT_LIST_HEAD(&ai->corr);
	INIT_LIST_HEAD(&ai->free);
	INIT_LIST_HEAD(&ai->erase);
	INIT_LIST_HEAD(&ai->alien);
	ai->volumes = RB_ROOT;
	ai->min_ec = UBI_MAX_ERASECOUNTER;

	ai->aeb_slab_cache = kmem_cache_create("ubi_ainf_peb_slab",
					       sizeof(struct ubi_ainf_peb),
					       0, 0, NULL);
	if (!ai->aeb_slab_cache) {
		ret = -ENOMEM;
		goto fail;
	}

	fmsb = (struct ubi_fm_sb *)(fm_raw);
	ai->max_sqnum = fmsb->sqnum;
	fm_pos += sizeof(struct ubi_fm_sb);
	if (fm_pos >= fm_size)
		goto fail_bad;

	fmhdr = (struct ubi_fm_hdr *)(fm_raw + fm_pos);
	fm_pos += sizeof(*fmhdr);
	if (fm_pos >= fm_size)
		goto fail_bad;

	if (be32_to_cpu(fmhdr->magic) != UBI_FM_HDR_MAGIC) {
		ubi_err(ubi, "bad fastmap header magic: 0x%x, expected: 0x%x",
			be32_to_cpu(fmhdr->magic), UBI_FM_HDR_MAGIC);
		goto fail_bad;
	}

	fmpl1 = (struct ubi_fm_scan_pool *)(fm_raw + fm_pos);
	fm_pos += sizeof(*fmpl1);
	if (fm_pos >= fm_size)
		goto fail_bad;
	if (be32_to_cpu(fmpl1->magic) != UBI_FM_POOL_MAGIC) {
		ubi_err(ubi, "bad fastmap pool magic: 0x%x, expected: 0x%x",
			be32_to_cpu(fmpl1->magic), UBI_FM_POOL_MAGIC);
		goto fail_bad;
	}

	fmpl2 = (struct ubi_fm_scan_pool *)(fm_raw + fm_pos);
	fm_pos += sizeof(*fmpl2);
	if (fm_pos >= fm_size)
		goto fail_bad;
	if (be32_to_cpu(fmpl2->magic) != UBI_FM_POOL_MAGIC) {
		ubi_err(ubi, "bad fastmap pool magic: 0x%x, expected: 0x%x",
			be32_to_cpu(fmpl2->magic), UBI_FM_POOL_MAGIC);
		goto fail_bad;
	}

	pool_size = be16_to_cpu(fmpl1->size);
	wl_pool_size = be16_to_cpu(fmpl2->size);
	fm->max_pool_size = be16_to_cpu(fmpl1->max_size);
	fm->max_wl_pool_size = be16_to_cpu(fmpl2->max_size);

	if (pool_size > UBI_FM_MAX_POOL_SIZE || pool_size < 0) {
		ubi_err(ubi, "bad pool size: %i", pool_size);
		goto fail_bad;
	}

	if (wl_pool_size > UBI_FM_MAX_POOL_SIZE || wl_pool_size < 0) {
		ubi_err(ubi, "bad WL pool size: %i", wl_pool_size);
		goto fail_bad;
	}


	if (fm->max_pool_size > UBI_FM_MAX_POOL_SIZE ||
	    fm->max_pool_size < 0) {
		ubi_err(ubi, "bad maximal pool size: %i", fm->max_pool_size);
		goto fail_bad;
	}

	if (fm->max_wl_pool_size > UBI_FM_MAX_POOL_SIZE ||
	    fm->max_wl_pool_size < 0) {
		ubi_err(ubi, "bad maximal WL pool size: %i",
			fm->max_wl_pool_size);
		goto fail_bad;
	}

	/* read EC values from free list */
	for (i = 0; i < be32_to_cpu(fmhdr->free_peb_count); i++) {
		fmec = (struct ubi_fm_ec *)(fm_raw + fm_pos);
		fm_pos += sizeof(*fmec);
		if (fm_pos >= fm_size)
			goto fail_bad;

		add_aeb(ai, &ai->free, be32_to_cpu(fmec->pnum),
			be32_to_cpu(fmec->ec), 0);
	}

	/* read EC values from used list */
	for (i = 0; i < be32_to_cpu(fmhdr->used_peb_count); i++) {
		fmec = (struct ubi_fm_ec *)(fm_raw + fm_pos);
		fm_pos += sizeof(*fmec);
		if (fm_pos >= fm_size)
			goto fail_bad;

		add_aeb(ai, &used, be32_to_cpu(fmec->pnum),
			be32_to_cpu(fmec->ec), 0);
	}

	/* read EC values from scrub list */
	for (i = 0; i < be32_to_cpu(fmhdr->scrub_peb_count); i++) {
		fmec = (struct ubi_fm_ec *)(fm_raw + fm_pos);
		fm_pos += sizeof(*fmec);
		if (fm_pos >= fm_size)
			goto fail_bad;

		add_aeb(ai, &used, be32_to_cpu(fmec->pnum),
			be32_to_cpu(fmec->ec), 1);
	}

	/* read EC values from erase list */
	for (i = 0; i < be32_to_cpu(fmhdr->erase_peb_count); i++) {
		fmec = (struct ubi_fm_ec *)(fm_raw + fm_pos);
		fm_pos += sizeof(*fmec);
		if (fm_pos >= fm_size)
			goto fail_bad;

		add_aeb(ai, &ai->erase, be32_to_cpu(fmec->pnum),
			be32_to_cpu(fmec->ec), 1);
	}

	ai->mean_ec = div_u64(ai->ec_sum, ai->ec_count);
	ai->bad_peb_count = be32_to_cpu(fmhdr->bad_peb_count);

	/* Iterate over all volumes and read their EBA table */
	for (i = 0; i < be32_to_cpu(fmhdr->vol_count); i++) {
		fmvhdr = (struct ubi_fm_volhdr *)(fm_raw + fm_pos);
		fm_pos += sizeof(*fmvhdr);
		if (fm_pos >= fm_size)
			goto fail_bad;

		if (be32_to_cpu(fmvhdr->magic) != UBI_FM_VHDR_MAGIC) {
			ubi_err(ubi, "bad fastmap vol header magic: 0x%x, expected: 0x%x",
				be32_to_cpu(fmvhdr->magic), UBI_FM_VHDR_MAGIC);
			goto fail_bad;
		}

		av = add_vol(ai, be32_to_cpu(fmvhdr->vol_id),
			     be32_to_cpu(fmvhdr->used_ebs),
			     be32_to_cpu(fmvhdr->data_pad),
			     fmvhdr->vol_type,
			     be32_to_cpu(fmvhdr->last_eb_bytes));

		if (!av)
			goto fail_bad;

		ai->vols_found++;
		if (ai->highest_vol_id < be32_to_cpu(fmvhdr->vol_id))
			ai->highest_vol_id = be32_to_cpu(fmvhdr->vol_id);

		fm_eba = (struct ubi_fm_eba *)(fm_raw + fm_pos);
		fm_pos += sizeof(*fm_eba);
		fm_pos += (sizeof(__be32) * be32_to_cpu(fm_eba->reserved_pebs));
		if (fm_pos >= fm_size)
			goto fail_bad;

		if (be32_to_cpu(fm_eba->magic) != UBI_FM_EBA_MAGIC) {
			ubi_err(ubi, "bad fastmap EBA header magic: 0x%x, expected: 0x%x",
				be32_to_cpu(fm_eba->magic), UBI_FM_EBA_MAGIC);
			goto fail_bad;
		}

		for (j = 0; j < be32_to_cpu(fm_eba->reserved_pebs); j++) {
			int pnum = be32_to_cpu(fm_eba->pnum[j]);

			if ((int)be32_to_cpu(fm_eba->pnum[j]) < 0)
				continue;

			aeb = NULL;
			list_for_each_entry(tmp_aeb, &used, u.list) {
				if (tmp_aeb->pnum == pnum) {
					aeb = tmp_aeb;
					break;
				}
			}

			/* This can happen if a PEB is already in an EBA known
			 * by this fastmap but the PEB itself is not in the used
			 * list.
			 * In this case the PEB can be within the fastmap pool
			 * or while writing the fastmap it was in the protection
			 * queue.
			 */
			if (!aeb) {
				aeb = kmem_cache_alloc(ai->aeb_slab_cache,
						       GFP_KERNEL);
				if (!aeb) {
					ret = -ENOMEM;

					goto fail;
				}

				aeb->lnum = j;
				aeb->pnum = be32_to_cpu(fm_eba->pnum[j]);
				aeb->ec = -1;
				aeb->scrub = aeb->copy_flag = aeb->sqnum = 0;
				list_add_tail(&aeb->u.list, &eba_orphans);
				continue;
			}

			aeb->lnum = j;

			if (av->highest_lnum <= aeb->lnum)
				av->highest_lnum = aeb->lnum;

			assign_aeb_to_av(ai, aeb, av);

			dbg_bld("inserting PEB:%i (LEB %i) to vol %i",
				aeb->pnum, aeb->lnum, av->vol_id);
		}

		ech = kzalloc(ubi->ec_hdr_alsize, GFP_KERNEL);
		if (!ech) {
			ret = -ENOMEM;
			goto fail;
		}

		list_for_each_entry_safe(tmp_aeb, _tmp_aeb, &eba_orphans,
					 u.list) {
			int err;

			if (ubi_io_is_bad(ubi, tmp_aeb->pnum)) {
				ubi_err(ubi, "bad PEB in fastmap EBA orphan list");
				ret = UBI_BAD_FASTMAP;
				kfree(ech);
				goto fail;
			}

			err = ubi_io_read_ec_hdr(ubi, tmp_aeb->pnum, ech, 0);
			if (err && err != UBI_IO_BITFLIPS) {
				ubi_err(ubi, "unable to read EC header! PEB:%i err:%i",
					tmp_aeb->pnum, err);
				ret = err > 0 ? UBI_BAD_FASTMAP : err;
				kfree(ech);

				goto fail;
			} else if (err == UBI_IO_BITFLIPS)
				tmp_aeb->scrub = 1;

			tmp_aeb->ec = be64_to_cpu(ech->ec);
			assign_aeb_to_av(ai, tmp_aeb, av);
		}

		kfree(ech);
	}

	ret = scan_pool(ubi, ai, fmpl1->pebs, pool_size, &max_sqnum,
			&eba_orphans, &free);
	if (ret)
		goto fail;

	ret = scan_pool(ubi, ai, fmpl2->pebs, wl_pool_size, &max_sqnum,
			&eba_orphans, &free);
	if (ret)
		goto fail;

	if (max_sqnum > ai->max_sqnum)
		ai->max_sqnum = max_sqnum;

	list_for_each_entry_safe(tmp_aeb, _tmp_aeb, &free, u.list)
		list_move_tail(&tmp_aeb->u.list, &ai->free);

	ubi_assert(list_empty(&used));
	ubi_assert(list_empty(&eba_orphans));
	ubi_assert(list_empty(&free));

	/*
	 * If fastmap is leaking PEBs (must not happen), raise a
	 * fat warning and fall back to scanning mode.
	 * We do this here because in ubi_wl_init() it's too late
	 * and we cannot fall back to scanning.
	 */
	if (WARN_ON(count_fastmap_pebs(ai) != ubi->peb_count -
		    ai->bad_peb_count - fm->used_blocks))
		goto fail_bad;

	return 0;

fail_bad:
	ret = UBI_BAD_FASTMAP;
fail:
	list_for_each_entry_safe(tmp_aeb, _tmp_aeb, &used, u.list) {
		list_del(&tmp_aeb->u.list);
		kmem_cache_free(ai->aeb_slab_cache, tmp_aeb);
	}
	list_for_each_entry_safe(tmp_aeb, _tmp_aeb, &eba_orphans, u.list) {
		list_del(&tmp_aeb->u.list);
		kmem_cache_free(ai->aeb_slab_cache, tmp_aeb);
	}
	list_for_each_entry_safe(tmp_aeb, _tmp_aeb, &free, u.list) {
		list_del(&tmp_aeb->u.list);
		kmem_cache_free(ai->aeb_slab_cache, tmp_aeb);
	}

	return ret;
}

/**
 * ubi_scan_fastmap - scan the fastmap.
 * @ubi: UBI device object
 * @ai: UBI attach info to be filled
 * @fm_anchor: The fastmap starts at this PEB
 *
 * Returns 0 on success, UBI_NO_FASTMAP if no fastmap was found,
 * UBI_BAD_FASTMAP if one was found but is not usable.
 * < 0 indicates an internal error.
 */
int ubi_scan_fastmap(struct ubi_device *ubi, struct ubi_attach_info *ai,
		     int fm_anchor)
{
	struct ubi_fm_sb *fmsb, *fmsb2;
	struct ubi_vid_hdr *vh;
	struct ubi_ec_hdr *ech;
	struct ubi_fastmap_layout *fm;
	int i, used_blocks, pnum, ret = 0;
	size_t fm_size;
	__be32 crc, tmp_crc;
	unsigned long long sqnum = 0;

	mutex_lock(&ubi->fm_mutex);
	memset(ubi->fm_buf, 0, ubi->fm_size);

	fmsb = kmalloc(sizeof(*fmsb), GFP_KERNEL);
	if (!fmsb) {
		ret = -ENOMEM;
		goto out;
	}

	fm = kzalloc(sizeof(*fm), GFP_KERNEL);
	if (!fm) {
		ret = -ENOMEM;
		kfree(fmsb);
		goto out;
	}

	ret = ubi_io_read(ubi, fmsb, fm_anchor, ubi->leb_start, sizeof(*fmsb));
	if (ret && ret != UBI_IO_BITFLIPS)
		goto free_fm_sb;
	else if (ret == UBI_IO_BITFLIPS)
		fm->to_be_tortured[0] = 1;

	if (be32_to_cpu(fmsb->magic) != UBI_FM_SB_MAGIC) {
		ubi_err(ubi, "bad super block magic: 0x%x, expected: 0x%x",
			be32_to_cpu(fmsb->magic), UBI_FM_SB_MAGIC);
		ret = UBI_BAD_FASTMAP;
		goto free_fm_sb;
	}

	if (fmsb->version != UBI_FM_FMT_VERSION) {
		ubi_err(ubi, "bad fastmap version: %i, expected: %i",
			fmsb->version, UBI_FM_FMT_VERSION);
		ret = UBI_BAD_FASTMAP;
		goto free_fm_sb;
	}

	used_blocks = be32_to_cpu(fmsb->used_blocks);
	if (used_blocks > UBI_FM_MAX_BLOCKS || used_blocks < 1) {
		ubi_err(ubi, "number of fastmap blocks is invalid: %i",
			used_blocks);
		ret = UBI_BAD_FASTMAP;
		goto free_fm_sb;
	}

	fm_size = ubi->leb_size * used_blocks;
	if (fm_size != ubi->fm_size) {
		ubi_err(ubi, "bad fastmap size: %zi, expected: %zi",
			fm_size, ubi->fm_size);
		ret = UBI_BAD_FASTMAP;
		goto free_fm_sb;
	}

	ech = kzalloc(ubi->ec_hdr_alsize, GFP_KERNEL);
	if (!ech) {
		ret = -ENOMEM;
		goto free_fm_sb;
	}

	vh = ubi_zalloc_vid_hdr(ubi, GFP_KERNEL);
	if (!vh) {
		ret = -ENOMEM;
		goto free_hdr;
	}

	for (i = 0; i < used_blocks; i++) {
		int image_seq;

		pnum = be32_to_cpu(fmsb->block_loc[i]);

		if (ubi_io_is_bad(ubi, pnum)) {
			ret = UBI_BAD_FASTMAP;
			goto free_hdr;
		}

		ret = ubi_io_read_ec_hdr(ubi, pnum, ech, 0);
		if (ret && ret != UBI_IO_BITFLIPS) {
			ubi_err(ubi, "unable to read fastmap block# %i EC (PEB: %i)",
				i, pnum);
			if (ret > 0)
				ret = UBI_BAD_FASTMAP;
			goto free_hdr;
		} else if (ret == UBI_IO_BITFLIPS)
			fm->to_be_tortured[i] = 1;

		image_seq = be32_to_cpu(ech->image_seq);
		if (!ubi->image_seq)
			ubi->image_seq = image_seq;

		/*
		 * Older UBI implementations have image_seq set to zero, so
		 * we shouldn't fail if image_seq == 0.
		 */
		if (image_seq && (image_seq != ubi->image_seq)) {
			ubi_err(ubi, "wrong image seq:%d instead of %d",
				be32_to_cpu(ech->image_seq), ubi->image_seq);
			ret = UBI_BAD_FASTMAP;
			goto free_hdr;
		}

		ret = ubi_io_read_vid_hdr(ubi, pnum, vh, 0);
		if (ret && ret != UBI_IO_BITFLIPS) {
			ubi_err(ubi, "unable to read fastmap block# %i (PEB: %i)",
				i, pnum);
			goto free_hdr;
		}

		if (i == 0) {
			if (be32_to_cpu(vh->vol_id) != UBI_FM_SB_VOLUME_ID) {
				ubi_err(ubi, "bad fastmap anchor vol_id: 0x%x, expected: 0x%x",
					be32_to_cpu(vh->vol_id),
					UBI_FM_SB_VOLUME_ID);
				ret = UBI_BAD_FASTMAP;
				goto free_hdr;
			}
		} else {
			if (be32_to_cpu(vh->vol_id) != UBI_FM_DATA_VOLUME_ID) {
				ubi_err(ubi, "bad fastmap data vol_id: 0x%x, expected: 0x%x",
					be32_to_cpu(vh->vol_id),
					UBI_FM_DATA_VOLUME_ID);
				ret = UBI_BAD_FASTMAP;
				goto free_hdr;
			}
		}

		if (sqnum < be64_to_cpu(vh->sqnum))
			sqnum = be64_to_cpu(vh->sqnum);

		ret = ubi_io_read(ubi, ubi->fm_buf + (ubi->leb_size * i), pnum,
				  ubi->leb_start, ubi->leb_size);
		if (ret && ret != UBI_IO_BITFLIPS) {
			ubi_err(ubi, "unable to read fastmap block# %i (PEB: %i, "
				"err: %i)", i, pnum, ret);
			goto free_hdr;
		}
	}

	kfree(fmsb);
	fmsb = NULL;

	fmsb2 = (struct ubi_fm_sb *)(ubi->fm_buf);
	tmp_crc = be32_to_cpu(fmsb2->data_crc);
	fmsb2->data_crc = 0;
	crc = crc32(UBI_CRC32_INIT, ubi->fm_buf, fm_size);
	if (crc != tmp_crc) {
		ubi_err(ubi, "fastmap data CRC is invalid");
		ubi_err(ubi, "CRC should be: 0x%x, calc: 0x%x",
			tmp_crc, crc);
		ret = UBI_BAD_FASTMAP;
		goto free_hdr;
	}

	fmsb2->sqnum = sqnum;

	fm->used_blocks = used_blocks;

	ret = ubi_attach_fastmap(ubi, ai, fm);
	if (ret) {
		if (ret > 0)
			ret = UBI_BAD_FASTMAP;
		goto free_hdr;
	}

	for (i = 0; i < used_blocks; i++) {
		struct ubi_wl_entry *e;

		e = kmem_cache_alloc(ubi_wl_entry_slab, GFP_KERNEL);
		if (!e) {
			while (i--)
				kfree(fm->e[i]);

			ret = -ENOMEM;
			goto free_hdr;
		}

		e->pnum = be32_to_cpu(fmsb2->block_loc[i]);
		e->ec = be32_to_cpu(fmsb2->block_ec[i]);
		fm->e[i] = e;
	}

	ubi->fm = fm;
	ubi->fm_pool.max_size = ubi->fm->max_pool_size;
	ubi->fm_wl_pool.max_size = ubi->fm->max_wl_pool_size;
	ubi_msg(ubi, "attached by fastmap");
	ubi_msg(ubi, "fastmap pool size: %d", ubi->fm_pool.max_size);
	ubi_msg(ubi, "fastmap WL pool size: %d",
		ubi->fm_wl_pool.max_size);
	ubi->fm_disabled = 0;

	ubi_free_vid_hdr(ubi, vh);
	kfree(ech);
out:
	mutex_unlock(&ubi->fm_mutex);
	if (ret == UBI_BAD_FASTMAP)
		ubi_err(ubi, "Attach by fastmap failed, doing a full scan!");
	return ret;

free_hdr:
	ubi_free_vid_hdr(ubi, vh);
	kfree(ech);
free_fm_sb:
	kfree(fmsb);
	kfree(fm);
	goto out;
}

/**
 * ubi_write_fastmap - writes a fastmap.
 * @ubi: UBI device object
 * @new_fm: the to be written fastmap
 *
 * Returns 0 on success, < 0 indicates an internal error.
 */
static int ubi_write_fastmap(struct ubi_device *ubi,
			     struct ubi_fastmap_layout *new_fm)
{
	size_t fm_pos = 0;
	void *fm_raw;
	struct ubi_fm_sb *fmsb;
	struct ubi_fm_hdr *fmh;
	struct ubi_fm_scan_pool *fmpl1, *fmpl2;
	struct ubi_fm_ec *fec;
	struct ubi_fm_volhdr *fvh;
	struct ubi_fm_eba *feba;
	struct rb_node *node;
	struct ubi_wl_entry *wl_e;
	struct ubi_volume *vol;
	struct ubi_vid_hdr *avhdr, *dvhdr;
	struct ubi_work *ubi_wrk;
	int ret, i, j, free_peb_count, used_peb_count, vol_count;
	int scrub_peb_count, erase_peb_count;

	fm_raw = ubi->fm_buf;
	memset(ubi->fm_buf, 0, ubi->fm_size);

	avhdr = new_fm_vhdr(ubi, UBI_FM_SB_VOLUME_ID);
	if (!avhdr) {
		ret = -ENOMEM;
		goto out;
	}

	dvhdr = new_fm_vhdr(ubi, UBI_FM_DATA_VOLUME_ID);
	if (!dvhdr) {
		ret = -ENOMEM;
		goto out_kfree;
	}

	spin_lock(&ubi->volumes_lock);
	spin_lock(&ubi->wl_lock);

	fmsb = (struct ubi_fm_sb *)fm_raw;
	fm_pos += sizeof(*fmsb);
	ubi_assert(fm_pos <= ubi->fm_size);

	fmh = (struct ubi_fm_hdr *)(fm_raw + fm_pos);
	fm_pos += sizeof(*fmh);
	ubi_assert(fm_pos <= ubi->fm_size);

	fmsb->magic = cpu_to_be32(UBI_FM_SB_MAGIC);
	fmsb->version = UBI_FM_FMT_VERSION;
	fmsb->used_blocks = cpu_to_be32(new_fm->used_blocks);
	/* the max sqnum will be filled in while *reading* the fastmap */
	fmsb->sqnum = 0;

	fmh->magic = cpu_to_be32(UBI_FM_HDR_MAGIC);
	free_peb_count = 0;
	used_peb_count = 0;
	scrub_peb_count = 0;
	erase_peb_count = 0;
	vol_count = 0;

	fmpl1 = (struct ubi_fm_scan_pool *)(fm_raw + fm_pos);
	fm_pos += sizeof(*fmpl1);
	fmpl1->magic = cpu_to_be32(UBI_FM_POOL_MAGIC);
	fmpl1->size = cpu_to_be16(ubi->fm_pool.size);
	fmpl1->max_size = cpu_to_be16(ubi->fm_pool.max_size);

	for (i = 0; i < ubi->fm_pool.size; i++)
		fmpl1->pebs[i] = cpu_to_be32(ubi->fm_pool.pebs[i]);

	fmpl2 = (struct ubi_fm_scan_pool *)(fm_raw + fm_pos);
	fm_pos += sizeof(*fmpl2);
	fmpl2->magic = cpu_to_be32(UBI_FM_POOL_MAGIC);
	fmpl2->size = cpu_to_be16(ubi->fm_wl_pool.size);
	fmpl2->max_size = cpu_to_be16(ubi->fm_wl_pool.max_size);

	for (i = 0; i < ubi->fm_wl_pool.size; i++)
		fmpl2->pebs[i] = cpu_to_be32(ubi->fm_wl_pool.pebs[i]);

	for (node = rb_first(&ubi->free); node; node = rb_next(node)) {
		wl_e = rb_entry(node, struct ubi_wl_entry, u.rb);
		fec = (struct ubi_fm_ec *)(fm_raw + fm_pos);

		fec->pnum = cpu_to_be32(wl_e->pnum);
		fec->ec = cpu_to_be32(wl_e->ec);

		free_peb_count++;
		fm_pos += sizeof(*fec);
		ubi_assert(fm_pos <= ubi->fm_size);
	}
	fmh->free_peb_count = cpu_to_be32(free_peb_count);

	for (node = rb_first(&ubi->used); node; node = rb_next(node)) {
		wl_e = rb_entry(node, struct ubi_wl_entry, u.rb);
		fec = (struct ubi_fm_ec *)(fm_raw + fm_pos);

		fec->pnum = cpu_to_be32(wl_e->pnum);
		fec->ec = cpu_to_be32(wl_e->ec);

		used_peb_count++;
		fm_pos += sizeof(*fec);
		ubi_assert(fm_pos <= ubi->fm_size);
	}
	fmh->used_peb_count = cpu_to_be32(used_peb_count);

	for (node = rb_first(&ubi->scrub); node; node = rb_next(node)) {
		wl_e = rb_entry(node, struct ubi_wl_entry, u.rb);
		fec = (struct ubi_fm_ec *)(fm_raw + fm_pos);

		fec->pnum = cpu_to_be32(wl_e->pnum);
		fec->ec = cpu_to_be32(wl_e->ec);

		scrub_peb_count++;
		fm_pos += sizeof(*fec);
		ubi_assert(fm_pos <= ubi->fm_size);
	}
	fmh->scrub_peb_count = cpu_to_be32(scrub_peb_count);


	list_for_each_entry(ubi_wrk, &ubi->works, list) {
		if (ubi_is_erase_work(ubi_wrk)) {
			wl_e = ubi_wrk->e;
			ubi_assert(wl_e);

			fec = (struct ubi_fm_ec *)(fm_raw + fm_pos);

			fec->pnum = cpu_to_be32(wl_e->pnum);
			fec->ec = cpu_to_be32(wl_e->ec);

			erase_peb_count++;
			fm_pos += sizeof(*fec);
			ubi_assert(fm_pos <= ubi->fm_size);
		}
	}
	fmh->erase_peb_count = cpu_to_be32(erase_peb_count);

	for (i = 0; i < UBI_MAX_VOLUMES + UBI_INT_VOL_COUNT; i++) {
		vol = ubi->volumes[i];

		if (!vol)
			continue;

		vol_count++;

		fvh = (struct ubi_fm_volhdr *)(fm_raw + fm_pos);
		fm_pos += sizeof(*fvh);
		ubi_assert(fm_pos <= ubi->fm_size);

		fvh->magic = cpu_to_be32(UBI_FM_VHDR_MAGIC);
		fvh->vol_id = cpu_to_be32(vol->vol_id);
		fvh->vol_type = vol->vol_type;
		fvh->used_ebs = cpu_to_be32(vol->used_ebs);
		fvh->data_pad = cpu_to_be32(vol->data_pad);
		fvh->last_eb_bytes = cpu_to_be32(vol->last_eb_bytes);

		ubi_assert(vol->vol_type == UBI_DYNAMIC_VOLUME ||
			vol->vol_type == UBI_STATIC_VOLUME);

		feba = (struct ubi_fm_eba *)(fm_raw + fm_pos);
		fm_pos += sizeof(*feba) + (sizeof(__be32) * vol->reserved_pebs);
		ubi_assert(fm_pos <= ubi->fm_size);

		for (j = 0; j < vol->reserved_pebs; j++)
			feba->pnum[j] = cpu_to_be32(vol->eba_tbl[j]);

		feba->reserved_pebs = cpu_to_be32(j);
		feba->magic = cpu_to_be32(UBI_FM_EBA_MAGIC);
	}
	fmh->vol_count = cpu_to_be32(vol_count);
	fmh->bad_peb_count = cpu_to_be32(ubi->bad_peb_count);

	avhdr->sqnum = cpu_to_be64(ubi_next_sqnum(ubi));
	avhdr->lnum = 0;

	spin_unlock(&ubi->wl_lock);
	spin_unlock(&ubi->volumes_lock);

	dbg_bld("writing fastmap SB to PEB %i", new_fm->e[0]->pnum);
	ret = ubi_io_write_vid_hdr(ubi, new_fm->e[0]->pnum, avhdr);
	if (ret) {
		ubi_err(ubi, "unable to write vid_hdr to fastmap SB!");
		goto out_kfree;
	}

	for (i = 0; i < new_fm->used_blocks; i++) {
		fmsb->block_loc[i] = cpu_to_be32(new_fm->e[i]->pnum);
		fmsb->block_ec[i] = cpu_to_be32(new_fm->e[i]->ec);
	}

	fmsb->data_crc = 0;
	fmsb->data_crc = cpu_to_be32(crc32(UBI_CRC32_INIT, fm_raw,
					   ubi->fm_size));

	for (i = 1; i < new_fm->used_blocks; i++) {
		dvhdr->sqnum = cpu_to_be64(ubi_next_sqnum(ubi));
		dvhdr->lnum = cpu_to_be32(i);
		dbg_bld("writing fastmap data to PEB %i sqnum %llu",
			new_fm->e[i]->pnum, be64_to_cpu(dvhdr->sqnum));
		ret = ubi_io_write_vid_hdr(ubi, new_fm->e[i]->pnum, dvhdr);
		if (ret) {
			ubi_err(ubi, "unable to write vid_hdr to PEB %i!",
				new_fm->e[i]->pnum);
			goto out_kfree;
		}
	}

	for (i = 0; i < new_fm->used_blocks; i++) {
		ret = ubi_io_write(ubi, fm_raw + (i * ubi->leb_size),
			new_fm->e[i]->pnum, ubi->leb_start, ubi->leb_size);
		if (ret) {
			ubi_err(ubi, "unable to write fastmap to PEB %i!",
				new_fm->e[i]->pnum);
			goto out_kfree;
		}
	}

	ubi_assert(new_fm);
	ubi->fm = new_fm;

	dbg_bld("fastmap written!");

out_kfree:
	ubi_free_vid_hdr(ubi, avhdr);
	ubi_free_vid_hdr(ubi, dvhdr);
out:
	return ret;
}

/**
 * erase_block - Manually erase a PEB.
 * @ubi: UBI device object
 * @pnum: PEB to be erased
 *
 * Returns the new EC value on success, < 0 indicates an internal error.
 */
static int erase_block(struct ubi_device *ubi, int pnum)
{
	int ret;
	struct ubi_ec_hdr *ec_hdr;
	long long ec;

	ec_hdr = kzalloc(ubi->ec_hdr_alsize, GFP_KERNEL);
	if (!ec_hdr)
		return -ENOMEM;

	ret = ubi_io_read_ec_hdr(ubi, pnum, ec_hdr, 0);
	if (ret < 0)
		goto out;
	else if (ret && ret != UBI_IO_BITFLIPS) {
		ret = -EINVAL;
		goto out;
	}

	ret = ubi_io_sync_erase(ubi, pnum, 0);
	if (ret < 0)
		goto out;

	ec = be64_to_cpu(ec_hdr->ec);
	ec += ret;
	if (ec > UBI_MAX_ERASECOUNTER) {
		ret = -EINVAL;
		goto out;
	}

	ec_hdr->ec = cpu_to_be64(ec);
	ret = ubi_io_write_ec_hdr(ubi, pnum, ec_hdr);
	if (ret < 0)
		goto out;

	ret = ec;
out:
	kfree(ec_hdr);
	return ret;
}

/**
 * invalidate_fastmap - destroys a fastmap.
 * @ubi: UBI device object
 * @fm: the fastmap to be destroyed
 *
 * Returns 0 on success, < 0 indicates an internal error.
 */
static int invalidate_fastmap(struct ubi_device *ubi,
			      struct ubi_fastmap_layout *fm)
{
	int ret;
	struct ubi_vid_hdr *vh;

	ret = erase_block(ubi, fm->e[0]->pnum);
	if (ret < 0)
		return ret;

	vh = new_fm_vhdr(ubi, UBI_FM_SB_VOLUME_ID);
	if (!vh)
		return -ENOMEM;

	/* deleting the current fastmap SB is not enough, an old SB may exist,
	 * so create a (corrupted) SB such that fastmap will find it and fall
	 * back to scanning mode in any case */
	vh->sqnum = cpu_to_be64(ubi_next_sqnum(ubi));
	ret = ubi_io_write_vid_hdr(ubi, fm->e[0]->pnum, vh);

	return ret;
}

/**
 * ubi_update_fastmap - will be called by UBI if a volume changes or
 * a fastmap pool becomes full.
 * @ubi: UBI device object
 *
 * Returns 0 on success, < 0 indicates an internal error.
 */
int ubi_update_fastmap(struct ubi_device *ubi)
{
	int ret, i;
	struct ubi_fastmap_layout *new_fm, *old_fm;
	struct ubi_wl_entry *tmp_e;

	mutex_lock(&ubi->fm_mutex);

	ubi_refill_pools(ubi);

	if (ubi->ro_mode || ubi->fm_disabled) {
		mutex_unlock(&ubi->fm_mutex);
		return 0;
	}

	ret = ubi_ensure_anchor_pebs(ubi);
	if (ret) {
		mutex_unlock(&ubi->fm_mutex);
		return ret;
	}

	new_fm = kzalloc(sizeof(*new_fm), GFP_KERNEL);
	if (!new_fm) {
		mutex_unlock(&ubi->fm_mutex);
		return -ENOMEM;
	}

	new_fm->used_blocks = ubi->fm_size / ubi->leb_size;

	for (i = 0; i < new_fm->used_blocks; i++) {
		new_fm->e[i] = kmem_cache_alloc(ubi_wl_entry_slab, GFP_KERNEL);
		if (!new_fm->e[i]) {
			while (i--)
				kfree(new_fm->e[i]);

			kfree(new_fm);
			mutex_unlock(&ubi->fm_mutex);
			return -ENOMEM;
		}
	}

	old_fm = ubi->fm;
	ubi->fm = NULL;

	if (new_fm->used_blocks > UBI_FM_MAX_BLOCKS) {
		ubi_err(ubi, "fastmap too large");
		ret = -ENOSPC;
		goto err;
	}

	for (i = 1; i < new_fm->used_blocks; i++) {
		spin_lock(&ubi->wl_lock);
		tmp_e = ubi_wl_get_fm_peb(ubi, 0);
		spin_unlock(&ubi->wl_lock);

		if (!tmp_e && !old_fm) {
			int j;
			ubi_err(ubi, "could not get any free erase block");

			for (j = 1; j < i; j++)
				ubi_wl_put_fm_peb(ubi, new_fm->e[j], j, 0);

			ret = -ENOSPC;
			goto err;
		} else if (!tmp_e && old_fm) {
			ret = erase_block(ubi, old_fm->e[i]->pnum);
			if (ret < 0) {
				int j;

				for (j = 1; j < i; j++)
					ubi_wl_put_fm_peb(ubi, new_fm->e[j],
							  j, 0);

				ubi_err(ubi, "could not erase old fastmap PEB");
				goto err;
			}

			new_fm->e[i]->pnum = old_fm->e[i]->pnum;
			new_fm->e[i]->ec = old_fm->e[i]->ec;
		} else {
			new_fm->e[i]->pnum = tmp_e->pnum;
			new_fm->e[i]->ec = tmp_e->ec;

			if (old_fm)
				ubi_wl_put_fm_peb(ubi, old_fm->e[i], i,
						  old_fm->to_be_tortured[i]);
		}
	}

	spin_lock(&ubi->wl_lock);
	tmp_e = ubi_wl_get_fm_peb(ubi, 1);
	spin_unlock(&ubi->wl_lock);

	if (old_fm) {
		/* no fresh anchor PEB was found, reuse the old one */
		if (!tmp_e) {
			ret = erase_block(ubi, old_fm->e[0]->pnum);
			if (ret < 0) {
				int i;
				ubi_err(ubi, "could not erase old anchor PEB");

				for (i = 1; i < new_fm->used_blocks; i++)
					ubi_wl_put_fm_peb(ubi, new_fm->e[i],
							  i, 0);
				goto err;
			}

			new_fm->e[0]->pnum = old_fm->e[0]->pnum;
			new_fm->e[0]->ec = ret;
		} else {
			/* we've got a new anchor PEB, return the old one */
			ubi_wl_put_fm_peb(ubi, old_fm->e[0], 0,
					  old_fm->to_be_tortured[0]);

			new_fm->e[0]->pnum = tmp_e->pnum;
			new_fm->e[0]->ec = tmp_e->ec;
		}
	} else {
		if (!tmp_e) {
			int i;
			ubi_err(ubi, "could not find any anchor PEB");

			for (i = 1; i < new_fm->used_blocks; i++)
				ubi_wl_put_fm_peb(ubi, new_fm->e[i], i, 0);

			ret = -ENOSPC;
			goto err;
		}

		new_fm->e[0]->pnum = tmp_e->pnum;
		new_fm->e[0]->ec = tmp_e->ec;
	}

	down_write(&ubi->work_sem);
	down_write(&ubi->fm_sem);
	ret = ubi_write_fastmap(ubi, new_fm);
	up_write(&ubi->fm_sem);
	up_write(&ubi->work_sem);

	if (ret)
		goto err;

out_unlock:
	mutex_unlock(&ubi->fm_mutex);
	kfree(old_fm);
	return ret;

err:
	kfree(new_fm);

	ubi_warn(ubi, "Unable to write new fastmap, err=%i", ret);

	ret = 0;
	if (old_fm) {
		ret = invalidate_fastmap(ubi, old_fm);
		if (ret < 0)
			ubi_err(ubi, "Unable to invalidiate current fastmap!");
		else if (ret)
			ret = 0;
	}
	goto out_unlock;
}
