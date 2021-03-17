// SPDX-License-Identifier: GPL-2.0
/*
 *  fs/partitions/aix.c
 *
 *  Copyright (C) 2012-2013 Philippe De Muyter <phdm@macqel.be>
 */

#include "check.h"

struct lvm_rec {
	char lvm_id[4]; /* "_LVM" */
	char reserved4[16];
	__be32 lvmarea_len;
	__be32 vgda_len;
	__be32 vgda_psn[2];
	char reserved36[10];
	__be16 pp_size; /* log2(pp_size) */
	char reserved46[12];
	__be16 version;
	};

struct vgda {
	__be32 secs;
	__be32 usec;
	char reserved8[16];
	__be16 numlvs;
	__be16 maxlvs;
	__be16 pp_size;
	__be16 numpvs;
	__be16 total_vgdas;
	__be16 vgda_size;
	};

struct lvd {
	__be16 lv_ix;
	__be16 res2;
	__be16 res4;
	__be16 maxsize;
	__be16 lv_state;
	__be16 mirror;
	__be16 mirror_policy;
	__be16 num_lps;
	__be16 res10[8];
	};

struct lvname {
	char name[64];
	};

struct ppe {
	__be16 lv_ix;
	unsigned short res2;
	unsigned short res4;
	__be16 lp_ix;
	unsigned short res8[12];
	};

struct pvd {
	char reserved0[16];
	__be16 pp_count;
	char reserved18[2];
	__be32 psn_part1;
	char reserved24[8];
	struct ppe ppe[1016];
	};

#define LVM_MAXLVS 256

/**
 * last_lba(): return number of last logical block of device
 * @bdev: block device
 *
 * Description: Returns last LBA value on success, 0 on error.
 * This is stored (by sd and ide-geometry) in
 *  the part[0] entry for this disk, and is the number of
 *  physical sectors available on the disk.
 */
static u64 last_lba(struct block_device *bdev)
{
	if (!bdev || !bdev->bd_inode)
		return 0;
	return (bdev->bd_inode->i_size >> 9) - 1ULL;
}

/**
 * read_lba(): Read bytes from disk, starting at given LBA
 * @state
 * @lba
 * @buffer
 * @count
 *
 * Description:  Reads @count bytes from @state->bdev into @buffer.
 * Returns number of bytes read on success, 0 on error.
 */
static size_t read_lba(struct parsed_partitions *state, u64 lba, u8 *buffer,
			size_t count)
{
	size_t totalreadcount = 0;

	if (!buffer || lba + count / 512 > last_lba(state->bdev))
		return 0;

	while (count) {
		int copied = 512;
		Sector sect;
		unsigned char *data = read_part_sector(state, lba++, &sect);
		if (!data)
			break;
		if (copied > count)
			copied = count;
		memcpy(buffer, data, copied);
		put_dev_sector(sect);
		buffer += copied;
		totalreadcount += copied;
		count -= copied;
	}
	return totalreadcount;
}

/**
 * alloc_pvd(): reads physical volume descriptor
 * @state
 * @lba
 *
 * Description: Returns pvd on success,  NULL on error.
 * Allocates space for pvd and fill it with disk blocks at @lba
 * Notes: remember to free pvd when you're done!
 */
static struct pvd *alloc_pvd(struct parsed_partitions *state, u32 lba)
{
	size_t count = sizeof(struct pvd);
	struct pvd *p;

	p = kmalloc(count, GFP_KERNEL);
	if (!p)
		return NULL;

	if (read_lba(state, lba, (u8 *) p, count) < count) {
		kfree(p);
		return NULL;
	}
	return p;
}

/**
 * alloc_lvn(): reads logical volume names
 * @state
 * @lba
 *
 * Description: Returns lvn on success,  NULL on error.
 * Allocates space for lvn and fill it with disk blocks at @lba
 * Notes: remember to free lvn when you're done!
 */
static struct lvname *alloc_lvn(struct parsed_partitions *state, u32 lba)
{
	size_t count = sizeof(struct lvname) * LVM_MAXLVS;
	struct lvname *p;

	p = kmalloc(count, GFP_KERNEL);
	if (!p)
		return NULL;

	if (read_lba(state, lba, (u8 *) p, count) < count) {
		kfree(p);
		return NULL;
	}
	return p;
}

int aix_partition(struct parsed_partitions *state)
{
	int ret = 0;
	Sector sect;
	unsigned char *d;
	u32 pp_bytes_size;
	u32 pp_blocks_size = 0;
	u32 vgda_sector = 0;
	u32 vgda_len = 0;
	int numlvs = 0;
	struct pvd *pvd = NULL;
	struct lv_info {
		unsigned short pps_per_lv;
		unsigned short pps_found;
		unsigned char lv_is_contiguous;
	} *lvip;
	struct lvname *n = NULL;

	d = read_part_sector(state, 7, &sect);
	if (d) {
		struct lvm_rec *p = (struct lvm_rec *)d;
		u16 lvm_version = be16_to_cpu(p->version);
		char tmp[64];

		if (lvm_version == 1) {
			int pp_size_log2 = be16_to_cpu(p->pp_size);

			pp_bytes_size = 1 << pp_size_log2;
			pp_blocks_size = pp_bytes_size / 512;
			snprintf(tmp, sizeof(tmp),
				" AIX LVM header version %u found\n",
				lvm_version);
			vgda_len = be32_to_cpu(p->vgda_len);
			vgda_sector = be32_to_cpu(p->vgda_psn[0]);
		} else {
			snprintf(tmp, sizeof(tmp),
				" unsupported AIX LVM version %d found\n",
				lvm_version);
		}
		strlcat(state->pp_buf, tmp, PAGE_SIZE);
		put_dev_sector(sect);
	}
	if (vgda_sector && (d = read_part_sector(state, vgda_sector, &sect))) {
		struct vgda *p = (struct vgda *)d;

		numlvs = be16_to_cpu(p->numlvs);
		put_dev_sector(sect);
	}
	lvip = kcalloc(state->limit, sizeof(struct lv_info), GFP_KERNEL);
	if (!lvip)
		return 0;
	if (numlvs && (d = read_part_sector(state, vgda_sector + 1, &sect))) {
		struct lvd *p = (struct lvd *)d;
		int i;

		n = alloc_lvn(state, vgda_sector + vgda_len - 33);
		if (n) {
			int foundlvs = 0;

			for (i = 0; foundlvs < numlvs && i < state->limit; i += 1) {
				lvip[i].pps_per_lv = be16_to_cpu(p[i].num_lps);
				if (lvip[i].pps_per_lv)
					foundlvs += 1;
			}
			/* pvd loops depend on n[].name and lvip[].pps_per_lv */
			pvd = alloc_pvd(state, vgda_sector + 17);
		}
		put_dev_sector(sect);
	}
	if (pvd) {
		int numpps = be16_to_cpu(pvd->pp_count);
		int psn_part1 = be32_to_cpu(pvd->psn_part1);
		int i;
		int cur_lv_ix = -1;
		int next_lp_ix = 1;
		int lp_ix;

		for (i = 0; i < numpps; i += 1) {
			struct ppe *p = pvd->ppe + i;
			unsigned int lv_ix;

			lp_ix = be16_to_cpu(p->lp_ix);
			if (!lp_ix) {
				next_lp_ix = 1;
				continue;
			}
			lv_ix = be16_to_cpu(p->lv_ix) - 1;
			if (lv_ix >= state->limit) {
				cur_lv_ix = -1;
				continue;
			}
			lvip[lv_ix].pps_found += 1;
			if (lp_ix == 1) {
				cur_lv_ix = lv_ix;
				next_lp_ix = 1;
			} else if (lv_ix != cur_lv_ix || lp_ix != next_lp_ix) {
				next_lp_ix = 1;
				continue;
			}
			if (lp_ix == lvip[lv_ix].pps_per_lv) {
				char tmp[70];

				put_partition(state, lv_ix + 1,
				  (i + 1 - lp_ix) * pp_blocks_size + psn_part1,
				  lvip[lv_ix].pps_per_lv * pp_blocks_size);
				snprintf(tmp, sizeof(tmp), " <%s>\n",
					 n[lv_ix].name);
				strlcat(state->pp_buf, tmp, PAGE_SIZE);
				lvip[lv_ix].lv_is_contiguous = 1;
				ret = 1;
				next_lp_ix = 1;
			} else
				next_lp_ix += 1;
		}
		for (i = 0; i < state->limit; i += 1)
			if (lvip[i].pps_found && !lvip[i].lv_is_contiguous) {
				char tmp[sizeof(n[i].name) + 1]; // null char

				snprintf(tmp, sizeof(tmp), "%s", n[i].name);
				pr_warn("partition %s (%u pp's found) is "
					"not contiguous\n",
					tmp, lvip[i].pps_found);
			}
		kfree(pvd);
	}
	kfree(n);
	kfree(lvip);
	return ret;
}
