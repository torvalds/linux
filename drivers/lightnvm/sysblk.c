/*
 * Copyright (C) 2015 Matias Bjorling. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */

#include <linux/lightnvm.h>

#define MAX_SYSBLKS 3	/* remember to update mapping scheme on change */
#define MAX_BLKS_PR_SYSBLK 2 /* 2 blks with 256 pages and 3000 erases
			      * enables ~1.5M updates per sysblk unit
			      */

struct sysblk_scan {
	/* A row is a collection of flash blocks for a system block. */
	int nr_rows;
	int row;
	int act_blk[MAX_SYSBLKS];

	int nr_ppas;
	struct ppa_addr ppas[MAX_SYSBLKS * MAX_BLKS_PR_SYSBLK];/* all sysblks */
};

static inline int scan_ppa_idx(int row, int blkid)
{
	return (row * MAX_BLKS_PR_SYSBLK) + blkid;
}

void nvm_sysblk_to_cpu(struct nvm_sb_info *info, struct nvm_system_block *sb)
{
	info->seqnr = be32_to_cpu(sb->seqnr);
	info->erase_cnt = be32_to_cpu(sb->erase_cnt);
	info->version = be16_to_cpu(sb->version);
	strncpy(info->mmtype, sb->mmtype, NVM_MMTYPE_LEN);
	info->fs_ppa.ppa = be64_to_cpu(sb->fs_ppa);
}

void nvm_cpu_to_sysblk(struct nvm_system_block *sb, struct nvm_sb_info *info)
{
	sb->magic = cpu_to_be32(NVM_SYSBLK_MAGIC);
	sb->seqnr = cpu_to_be32(info->seqnr);
	sb->erase_cnt = cpu_to_be32(info->erase_cnt);
	sb->version = cpu_to_be16(info->version);
	strncpy(sb->mmtype, info->mmtype, NVM_MMTYPE_LEN);
	sb->fs_ppa = cpu_to_be64(info->fs_ppa.ppa);
}

static int nvm_setup_sysblks(struct nvm_dev *dev, struct ppa_addr *sysblk_ppas)
{
	int nr_rows = min_t(int, MAX_SYSBLKS, dev->nr_chnls);
	int i;

	for (i = 0; i < nr_rows; i++)
		sysblk_ppas[i].ppa = 0;

	/* if possible, place sysblk at first channel, middle channel and last
	 * channel of the device. If not, create only one or two sys blocks
	 */
	switch (dev->nr_chnls) {
	case 2:
		sysblk_ppas[1].g.ch = 1;
		/* fall-through */
	case 1:
		sysblk_ppas[0].g.ch = 0;
		break;
	default:
		sysblk_ppas[0].g.ch = 0;
		sysblk_ppas[1].g.ch = dev->nr_chnls / 2;
		sysblk_ppas[2].g.ch = dev->nr_chnls - 1;
		break;
	}

	return nr_rows;
}

void nvm_setup_sysblk_scan(struct nvm_dev *dev, struct sysblk_scan *s,
						struct ppa_addr *sysblk_ppas)
{
	memset(s, 0, sizeof(struct sysblk_scan));
	s->nr_rows = nvm_setup_sysblks(dev, sysblk_ppas);
}

static int sysblk_get_host_blks(struct ppa_addr ppa, int nr_blks, u8 *blks,
								void *private)
{
	struct sysblk_scan *s = private;
	int i, nr_sysblk = 0;

	for (i = 0; i < nr_blks; i++) {
		if (blks[i] != NVM_BLK_T_HOST)
			continue;

		if (s->nr_ppas == MAX_BLKS_PR_SYSBLK * MAX_SYSBLKS) {
			pr_err("nvm: too many host blks\n");
			return -EINVAL;
		}

		ppa.g.blk = i;

		s->ppas[scan_ppa_idx(s->row, nr_sysblk)] = ppa;
		s->nr_ppas++;
		nr_sysblk++;
	}

	return 0;
}

static int nvm_get_all_sysblks(struct nvm_dev *dev, struct sysblk_scan *s,
				struct ppa_addr *ppas, nvm_bb_update_fn *fn)
{
	struct ppa_addr dppa;
	int i, ret;

	s->nr_ppas = 0;

	for (i = 0; i < s->nr_rows; i++) {
		dppa = generic_to_dev_addr(dev, ppas[i]);
		s->row = i;

		ret = dev->ops->get_bb_tbl(dev, dppa, dev->blks_per_lun, fn, s);
		if (ret) {
			pr_err("nvm: failed bb tbl for ppa (%u %u)\n",
							ppas[i].g.ch,
							ppas[i].g.blk);
			return ret;
		}
	}

	return ret;
}

/*
 * scans a block for latest sysblk.
 * Returns:
 *	0 - newer sysblk not found. PPA is updated to latest page.
 *	1 - newer sysblk found and stored in *cur. PPA is updated to
 *	    next valid page.
 *	<0- error.
 */
static int nvm_scan_block(struct nvm_dev *dev, struct ppa_addr *ppa,
						struct nvm_system_block *sblk)
{
	struct nvm_system_block *cur;
	int pg, cursz, ret, found = 0;

	/* the full buffer for a flash page is allocated. Only the first of it
	 * contains the system block information
	 */
	cursz = dev->sec_size * dev->sec_per_pg * dev->nr_planes;
	cur = kmalloc(cursz, GFP_KERNEL);
	if (!cur)
		return -ENOMEM;

	/* perform linear scan through the block */
	for (pg = 0; pg < dev->lps_per_blk; pg++) {
		ppa->g.pg = ppa_to_slc(dev, pg);

		ret = nvm_submit_ppa(dev, ppa, 1, NVM_OP_PREAD, NVM_IO_SLC_MODE,
								cur, cursz);
		if (ret) {
			if (ret == NVM_RSP_ERR_EMPTYPAGE) {
				pr_debug("nvm: sysblk scan empty ppa (%u %u %u %u)\n",
							ppa->g.ch,
							ppa->g.lun,
							ppa->g.blk,
							ppa->g.pg);
				break;
			}
			pr_err("nvm: read failed (%x) for ppa (%u %u %u %u)",
							ret,
							ppa->g.ch,
							ppa->g.lun,
							ppa->g.blk,
							ppa->g.pg);
			break; /* if we can't read a page, continue to the
				* next blk
				*/
		}

		if (be32_to_cpu(cur->magic) != NVM_SYSBLK_MAGIC) {
			pr_debug("nvm: scan break for ppa (%u %u %u %u)\n",
							ppa->g.ch,
							ppa->g.lun,
							ppa->g.blk,
							ppa->g.pg);
			break; /* last valid page already found */
		}

		if (be32_to_cpu(cur->seqnr) < be32_to_cpu(sblk->seqnr))
			continue;

		memcpy(sblk, cur, sizeof(struct nvm_system_block));
		found = 1;
	}

	kfree(cur);

	return found;
}

static int nvm_set_bb_tbl(struct nvm_dev *dev, struct sysblk_scan *s, int type)
{
	struct nvm_rq rqd;
	int ret;

	if (s->nr_ppas > dev->ops->max_phys_sect) {
		pr_err("nvm: unable to update all sysblocks atomically\n");
		return -EINVAL;
	}

	memset(&rqd, 0, sizeof(struct nvm_rq));

	nvm_set_rqd_ppalist(dev, &rqd, s->ppas, s->nr_ppas);
	nvm_generic_to_addr_mode(dev, &rqd);

	ret = dev->ops->set_bb_tbl(dev, &rqd, type);
	nvm_free_rqd_ppalist(dev, &rqd);
	if (ret) {
		pr_err("nvm: sysblk failed bb mark\n");
		return -EINVAL;
	}

	return 0;
}

static int sysblk_get_free_blks(struct ppa_addr ppa, int nr_blks, u8 *blks,
								void *private)
{
	struct sysblk_scan *s = private;
	struct ppa_addr *sppa;
	int i, blkid = 0;

	for (i = 0; i < nr_blks; i++) {
		if (blks[i] == NVM_BLK_T_HOST)
			return -EEXIST;

		if (blks[i] != NVM_BLK_T_FREE)
			continue;

		sppa = &s->ppas[scan_ppa_idx(s->row, blkid)];
		sppa->g.ch = ppa.g.ch;
		sppa->g.lun = ppa.g.lun;
		sppa->g.blk = i;
		s->nr_ppas++;
		blkid++;

		pr_debug("nvm: use (%u %u %u) as sysblk\n",
					sppa->g.ch, sppa->g.lun, sppa->g.blk);
		if (blkid > MAX_BLKS_PR_SYSBLK - 1)
			return 0;
	}

	pr_err("nvm: sysblk failed get sysblk\n");
	return -EINVAL;
}

static int nvm_write_and_verify(struct nvm_dev *dev, struct nvm_sb_info *info,
							struct sysblk_scan *s)
{
	struct nvm_system_block nvmsb;
	void *buf;
	int i, sect, ret, bufsz;
	struct ppa_addr *ppas;

	nvm_cpu_to_sysblk(&nvmsb, info);

	/* buffer for flash page */
	bufsz = dev->sec_size * dev->sec_per_pg * dev->nr_planes;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	memcpy(buf, &nvmsb, sizeof(struct nvm_system_block));

	ppas = kcalloc(dev->sec_per_pg, sizeof(struct ppa_addr), GFP_KERNEL);
	if (!ppas) {
		ret = -ENOMEM;
		goto err;
	}

	/* Write and verify */
	for (i = 0; i < s->nr_rows; i++) {
		ppas[0] = s->ppas[scan_ppa_idx(i, s->act_blk[i])];

		pr_debug("nvm: writing sysblk to ppa (%u %u %u %u)\n",
							ppas[0].g.ch,
							ppas[0].g.lun,
							ppas[0].g.blk,
							ppas[0].g.pg);

		/* Expand to all sectors within a flash page */
		if (dev->sec_per_pg > 1) {
			for (sect = 1; sect < dev->sec_per_pg; sect++) {
				ppas[sect].ppa = ppas[0].ppa;
				ppas[sect].g.sec = sect;
			}
		}

		ret = nvm_submit_ppa(dev, ppas, dev->sec_per_pg, NVM_OP_PWRITE,
						NVM_IO_SLC_MODE, buf, bufsz);
		if (ret) {
			pr_err("nvm: sysblk failed program (%u %u %u)\n",
							ppas[0].g.ch,
							ppas[0].g.lun,
							ppas[0].g.blk);
			break;
		}

		ret = nvm_submit_ppa(dev, ppas, dev->sec_per_pg, NVM_OP_PREAD,
						NVM_IO_SLC_MODE, buf, bufsz);
		if (ret) {
			pr_err("nvm: sysblk failed read (%u %u %u)\n",
							ppas[0].g.ch,
							ppas[0].g.lun,
							ppas[0].g.blk);
			break;
		}

		if (memcmp(buf, &nvmsb, sizeof(struct nvm_system_block))) {
			pr_err("nvm: sysblk failed verify (%u %u %u)\n",
							ppas[0].g.ch,
							ppas[0].g.lun,
							ppas[0].g.blk);
			ret = -EINVAL;
			break;
		}
	}

	kfree(ppas);
err:
	kfree(buf);

	return ret;
}

static int nvm_prepare_new_sysblks(struct nvm_dev *dev, struct sysblk_scan *s)
{
	int i, ret;
	unsigned long nxt_blk;
	struct ppa_addr *ppa;

	for (i = 0; i < s->nr_rows; i++) {
		nxt_blk = (s->act_blk[i] + 1) % MAX_BLKS_PR_SYSBLK;
		ppa = &s->ppas[scan_ppa_idx(i, nxt_blk)];
		ppa->g.pg = ppa_to_slc(dev, 0);

		ret = nvm_erase_ppa(dev, ppa, 1);
		if (ret)
			return ret;

		s->act_blk[i] = nxt_blk;
	}

	return 0;
}

int nvm_get_sysblock(struct nvm_dev *dev, struct nvm_sb_info *info)
{
	struct ppa_addr sysblk_ppas[MAX_SYSBLKS];
	struct sysblk_scan s;
	struct nvm_system_block *cur;
	int i, j, found = 0;
	int ret = -ENOMEM;

	/*
	 * 1. setup sysblk locations
	 * 2. get bad block list
	 * 3. filter on host-specific (type 3)
	 * 4. iterate through all and find the highest seq nr.
	 * 5. return superblock information
	 */

	if (!dev->ops->get_bb_tbl)
		return -EINVAL;

	nvm_setup_sysblk_scan(dev, &s, sysblk_ppas);

	mutex_lock(&dev->mlock);
	ret = nvm_get_all_sysblks(dev, &s, sysblk_ppas, sysblk_get_host_blks);
	if (ret)
		goto err_sysblk;

	/* no sysblocks initialized */
	if (!s.nr_ppas)
		goto err_sysblk;

	cur = kzalloc(sizeof(struct nvm_system_block), GFP_KERNEL);
	if (!cur)
		goto err_sysblk;

	/* find the latest block across all sysblocks */
	for (i = 0; i < s.nr_rows; i++) {
		for (j = 0; j < MAX_BLKS_PR_SYSBLK; j++) {
			struct ppa_addr ppa = s.ppas[scan_ppa_idx(i, j)];

			ret = nvm_scan_block(dev, &ppa, cur);
			if (ret > 0)
				found = 1;
			else if (ret < 0)
				break;
		}
	}

	nvm_sysblk_to_cpu(info, cur);

	kfree(cur);
err_sysblk:
	mutex_unlock(&dev->mlock);

	if (found)
		return 1;
	return ret;
}

int nvm_update_sysblock(struct nvm_dev *dev, struct nvm_sb_info *new)
{
	/* 1. for each latest superblock
	 * 2. if room
	 *    a. write new flash page entry with the updated information
	 * 3. if no room
	 *    a. find next available block on lun (linear search)
	 *       if none, continue to next lun
	 *       if none at all, report error. also report that it wasn't
	 *       possible to write to all superblocks.
	 *    c. write data to block.
	 */
	struct ppa_addr sysblk_ppas[MAX_SYSBLKS];
	struct sysblk_scan s;
	struct nvm_system_block *cur;
	int i, j, ppaidx, found = 0;
	int ret = -ENOMEM;

	if (!dev->ops->get_bb_tbl)
		return -EINVAL;

	nvm_setup_sysblk_scan(dev, &s, sysblk_ppas);

	mutex_lock(&dev->mlock);
	ret = nvm_get_all_sysblks(dev, &s, sysblk_ppas, sysblk_get_host_blks);
	if (ret)
		goto err_sysblk;

	cur = kzalloc(sizeof(struct nvm_system_block), GFP_KERNEL);
	if (!cur)
		goto err_sysblk;

	/* Get the latest sysblk for each sysblk row */
	for (i = 0; i < s.nr_rows; i++) {
		found = 0;
		for (j = 0; j < MAX_BLKS_PR_SYSBLK; j++) {
			ppaidx = scan_ppa_idx(i, j);
			ret = nvm_scan_block(dev, &s.ppas[ppaidx], cur);
			if (ret > 0) {
				s.act_blk[i] = j;
				found = 1;
			} else if (ret < 0)
				break;
		}
	}

	if (!found) {
		pr_err("nvm: no valid sysblks found to update\n");
		ret = -EINVAL;
		goto err_cur;
	}

	/*
	 * All sysblocks found. Check that they have same page id in their flash
	 * blocks
	 */
	for (i = 1; i < s.nr_rows; i++) {
		struct ppa_addr l = s.ppas[scan_ppa_idx(0, s.act_blk[0])];
		struct ppa_addr r = s.ppas[scan_ppa_idx(i, s.act_blk[i])];

		if (l.g.pg != r.g.pg) {
			pr_err("nvm: sysblks not on same page. Previous update failed.\n");
			ret = -EINVAL;
			goto err_cur;
		}
	}

	/*
	 * Check that there haven't been another update to the seqnr since we
	 * began
	 */
	if ((new->seqnr - 1) != be32_to_cpu(cur->seqnr)) {
		pr_err("nvm: seq is not sequential\n");
		ret = -EINVAL;
		goto err_cur;
	}

	/*
	 * When all pages in a block has been written, a new block is selected
	 * and writing is performed on the new block.
	 */
	if (s.ppas[scan_ppa_idx(0, s.act_blk[0])].g.pg ==
						dev->lps_per_blk - 1) {
		ret = nvm_prepare_new_sysblks(dev, &s);
		if (ret)
			goto err_cur;
	}

	ret = nvm_write_and_verify(dev, new, &s);
err_cur:
	kfree(cur);
err_sysblk:
	mutex_unlock(&dev->mlock);

	return ret;
}

int nvm_init_sysblock(struct nvm_dev *dev, struct nvm_sb_info *info)
{
	struct ppa_addr sysblk_ppas[MAX_SYSBLKS];
	struct sysblk_scan s;
	int ret;

	/*
	 * 1. select master blocks and select first available blks
	 * 2. get bad block list
	 * 3. mark MAX_SYSBLKS block as host-based device allocated.
	 * 4. write and verify data to block
	 */

	if (!dev->ops->get_bb_tbl || !dev->ops->set_bb_tbl)
		return -EINVAL;

	if (!(dev->mccap & NVM_ID_CAP_SLC) || !dev->lps_per_blk) {
		pr_err("nvm: memory does not support SLC access\n");
		return -EINVAL;
	}

	/* Index all sysblocks and mark them as host-driven */
	nvm_setup_sysblk_scan(dev, &s, sysblk_ppas);

	mutex_lock(&dev->mlock);
	ret = nvm_get_all_sysblks(dev, &s, sysblk_ppas, sysblk_get_free_blks);
	if (ret)
		goto err_mark;

	ret = nvm_set_bb_tbl(dev, &s, NVM_BLK_T_HOST);
	if (ret)
		goto err_mark;

	/* Write to the first block of each row */
	ret = nvm_write_and_verify(dev, info, &s);
err_mark:
	mutex_unlock(&dev->mlock);
	return ret;
}
