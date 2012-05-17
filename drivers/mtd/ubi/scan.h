/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

#ifndef __UBI_SCAN_H__
#define __UBI_SCAN_H__

/* The erase counter value for this physical eraseblock is unknown */
#define UBI_SCAN_UNKNOWN_EC (-1)

/**
 * struct ubi_ainf_peb - attach information about a physical eraseblock.
 * @ec: erase counter (%UBI_SCAN_UNKNOWN_EC if it is unknown)
 * @pnum: physical eraseblock number
 * @lnum: logical eraseblock number
 * @scrub: if this physical eraseblock needs scrubbing
 * @copy_flag: this LEB is a copy (@copy_flag is set in VID header of this LEB)
 * @sqnum: sequence number
 * @u: unions RB-tree or @list links
 * @u.rb: link in the per-volume RB-tree of &struct ubi_ainf_peb objects
 * @u.list: link in one of the eraseblock lists
 *
 * One object of this type is allocated for each physical eraseblock when
 * attaching an MTD device.
 */
struct ubi_ainf_peb {
	int ec;
	int pnum;
	int lnum;
	unsigned int scrub:1;
	unsigned int copy_flag:1;
	unsigned long long sqnum;
	union {
		struct rb_node rb;
		struct list_head list;
	} u;
};

/**
 * struct ubi_ainf_volume - attaching information about a volume.
 * @vol_id: volume ID
 * @highest_lnum: highest logical eraseblock number in this volume
 * @leb_count: number of logical eraseblocks in this volume
 * @vol_type: volume type
 * @used_ebs: number of used logical eraseblocks in this volume (only for
 *            static volumes)
 * @last_data_size: amount of data in the last logical eraseblock of this
 *                  volume (always equivalent to the usable logical eraseblock
 *                  size in case of dynamic volumes)
 * @data_pad: how many bytes at the end of logical eraseblocks of this volume
 *            are not used (due to volume alignment)
 * @compat: compatibility flags of this volume
 * @rb: link in the volume RB-tree
 * @root: root of the RB-tree containing all the eraseblock belonging to this
 *        volume (&struct ubi_ainf_peb objects)
 *
 * One object of this type is allocated for each volume when attaching an MTD
 * device.
 */
struct ubi_ainf_volume {
	int vol_id;
	int highest_lnum;
	int leb_count;
	int vol_type;
	int used_ebs;
	int last_data_size;
	int data_pad;
	int compat;
	struct rb_node rb;
	struct rb_root root;
};

/**
 * struct ubi_attach_info - MTD device attaching information.
 * @volumes: root of the volume RB-tree
 * @corr: list of corrupted physical eraseblocks
 * @free: list of free physical eraseblocks
 * @erase: list of physical eraseblocks which have to be erased
 * @alien: list of physical eraseblocks which should not be used by UBI (e.g.,
 *         those belonging to "preserve"-compatible internal volumes)
 * @corr_peb_count: count of PEBs in the @corr list
 * @empty_peb_count: count of PEBs which are presumably empty (contain only
 *                   0xFF bytes)
 * @alien_peb_count: count of PEBs in the @alien list
 * @bad_peb_count: count of bad physical eraseblocks
 * @maybe_bad_peb_count: count of bad physical eraseblocks which are not marked
 *                       as bad yet, but which look like bad
 * @vols_found: number of volumes found
 * @highest_vol_id: highest volume ID
 * @is_empty: flag indicating whether the MTD device is empty or not
 * @min_ec: lowest erase counter value
 * @max_ec: highest erase counter value
 * @max_sqnum: highest sequence number value
 * @mean_ec: mean erase counter value
 * @ec_sum: a temporary variable used when calculating @mean_ec
 * @ec_count: a temporary variable used when calculating @mean_ec
 * @scan_leb_slab: slab cache for &struct ubi_ainf_peb objects
 *
 * This data structure contains the result of attaching an MTD device and may
 * be used by other UBI sub-systems to build final UBI data structures, further
 * error-recovery and so on.
 */
struct ubi_attach_info {
	struct rb_root volumes;
	struct list_head corr;
	struct list_head free;
	struct list_head erase;
	struct list_head alien;
	int corr_peb_count;
	int empty_peb_count;
	int alien_peb_count;
	int bad_peb_count;
	int maybe_bad_peb_count;
	int vols_found;
	int highest_vol_id;
	int is_empty;
	int min_ec;
	int max_ec;
	unsigned long long max_sqnum;
	int mean_ec;
	uint64_t ec_sum;
	int ec_count;
	struct kmem_cache *scan_leb_slab;
};

struct ubi_device;
struct ubi_vid_hdr;

/*
 * ubi_scan_move_to_list - move a PEB from the volume tree to a list.
 *
 * @av: volume attaching information
 * @aeb: scanning eraseblock information
 * @list: the list to move to
 */
static inline void ubi_scan_move_to_list(struct ubi_ainf_volume *av,
					 struct ubi_ainf_peb *aeb,
					 struct list_head *list)
{
		rb_erase(&aeb->u.rb, &av->root);
		list_add_tail(&aeb->u.list, list);
}

int ubi_add_to_av(struct ubi_device *ubi, struct ubi_attach_info *ai, int pnum,
		  int ec, const struct ubi_vid_hdr *vid_hdr, int bitflips);
struct ubi_ainf_volume *ubi_find_av(const struct ubi_attach_info *ai,
				    int vol_id);
void ubi_scan_rm_volume(struct ubi_attach_info *ai, struct ubi_ainf_volume *av);
struct ubi_ainf_peb *ubi_scan_get_free_peb(struct ubi_device *ubi,
					   struct ubi_attach_info *ai);
struct ubi_attach_info *ubi_scan(struct ubi_device *ubi);
void ubi_scan_destroy_ai(struct ubi_attach_info *ai);

#endif /* !__UBI_SCAN_H__ */
