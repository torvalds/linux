/*
 * Copyright (C) 2015 IT University of Copenhagen. All rights reserved.
 * Initial release: Matias Bjorling <m@bjorling.me>
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

#include <linux/list.h>
#include <linux/types.h>
#include <linux/sem.h>
#include <linux/bitmap.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/lightnvm.h>
#include <linux/sched/sysctl.h>

static LIST_HEAD(nvm_tgt_types);
static DECLARE_RWSEM(nvm_tgtt_lock);
static LIST_HEAD(nvm_mgrs);
static LIST_HEAD(nvm_devices);
static DECLARE_RWSEM(nvm_lock);

struct nvm_tgt_type *nvm_find_target_type(const char *name, int lock)
{
	struct nvm_tgt_type *tmp, *tt = NULL;

	if (lock)
		down_write(&nvm_tgtt_lock);

	list_for_each_entry(tmp, &nvm_tgt_types, list)
		if (!strcmp(name, tmp->name)) {
			tt = tmp;
			break;
		}

	if (lock)
		up_write(&nvm_tgtt_lock);
	return tt;
}
EXPORT_SYMBOL(nvm_find_target_type);

int nvm_register_tgt_type(struct nvm_tgt_type *tt)
{
	int ret = 0;

	down_write(&nvm_tgtt_lock);
	if (nvm_find_target_type(tt->name, 0))
		ret = -EEXIST;
	else
		list_add(&tt->list, &nvm_tgt_types);
	up_write(&nvm_tgtt_lock);

	return ret;
}
EXPORT_SYMBOL(nvm_register_tgt_type);

void nvm_unregister_tgt_type(struct nvm_tgt_type *tt)
{
	if (!tt)
		return;

	down_write(&nvm_lock);
	list_del(&tt->list);
	up_write(&nvm_lock);
}
EXPORT_SYMBOL(nvm_unregister_tgt_type);

void *nvm_dev_dma_alloc(struct nvm_dev *dev, gfp_t mem_flags,
							dma_addr_t *dma_handler)
{
	return dev->ops->dev_dma_alloc(dev, dev->dma_pool, mem_flags,
								dma_handler);
}
EXPORT_SYMBOL(nvm_dev_dma_alloc);

void nvm_dev_dma_free(struct nvm_dev *dev, void *addr, dma_addr_t dma_handler)
{
	dev->ops->dev_dma_free(dev->dma_pool, addr, dma_handler);
}
EXPORT_SYMBOL(nvm_dev_dma_free);

static struct nvmm_type *nvm_find_mgr_type(const char *name)
{
	struct nvmm_type *mt;

	list_for_each_entry(mt, &nvm_mgrs, list)
		if (!strcmp(name, mt->name))
			return mt;

	return NULL;
}

static struct nvmm_type *nvm_init_mgr(struct nvm_dev *dev)
{
	struct nvmm_type *mt;
	int ret;

	lockdep_assert_held(&nvm_lock);

	list_for_each_entry(mt, &nvm_mgrs, list) {
		if (strncmp(dev->sb.mmtype, mt->name, NVM_MMTYPE_LEN))
			continue;

		ret = mt->register_mgr(dev);
		if (ret < 0) {
			pr_err("nvm: media mgr failed to init (%d) on dev %s\n",
								ret, dev->name);
			return NULL; /* initialization failed */
		} else if (ret > 0)
			return mt;
	}

	return NULL;
}

int nvm_register_mgr(struct nvmm_type *mt)
{
	struct nvm_dev *dev;
	int ret = 0;

	down_write(&nvm_lock);
	if (nvm_find_mgr_type(mt->name)) {
		ret = -EEXIST;
		goto finish;
	} else {
		list_add(&mt->list, &nvm_mgrs);
	}

	/* try to register media mgr if any device have none configured */
	list_for_each_entry(dev, &nvm_devices, devices) {
		if (dev->mt)
			continue;

		dev->mt = nvm_init_mgr(dev);
	}
finish:
	up_write(&nvm_lock);

	return ret;
}
EXPORT_SYMBOL(nvm_register_mgr);

void nvm_unregister_mgr(struct nvmm_type *mt)
{
	if (!mt)
		return;

	down_write(&nvm_lock);
	list_del(&mt->list);
	up_write(&nvm_lock);
}
EXPORT_SYMBOL(nvm_unregister_mgr);

static struct nvm_dev *nvm_find_nvm_dev(const char *name)
{
	struct nvm_dev *dev;

	list_for_each_entry(dev, &nvm_devices, devices)
		if (!strcmp(name, dev->name))
			return dev;

	return NULL;
}

static void nvm_tgt_generic_to_addr_mode(struct nvm_tgt_dev *tgt_dev,
					 struct nvm_rq *rqd)
{
	struct nvm_dev *dev = tgt_dev->parent;
	int i;

	if (rqd->nr_ppas > 1) {
		for (i = 0; i < rqd->nr_ppas; i++) {
			rqd->ppa_list[i] = dev->mt->trans_ppa(tgt_dev,
					rqd->ppa_list[i], TRANS_TGT_TO_DEV);
			rqd->ppa_list[i] = generic_to_dev_addr(dev,
							rqd->ppa_list[i]);
		}
	} else {
		rqd->ppa_addr = dev->mt->trans_ppa(tgt_dev, rqd->ppa_addr,
						TRANS_TGT_TO_DEV);
		rqd->ppa_addr = generic_to_dev_addr(dev, rqd->ppa_addr);
	}
}

int nvm_set_bb_tbl(struct nvm_dev *dev, struct ppa_addr *ppas, int nr_ppas,
								int type)
{
	struct nvm_rq rqd;
	int ret;

	if (nr_ppas > dev->ops->max_phys_sect) {
		pr_err("nvm: unable to update all sysblocks atomically\n");
		return -EINVAL;
	}

	memset(&rqd, 0, sizeof(struct nvm_rq));

	nvm_set_rqd_ppalist(dev, &rqd, ppas, nr_ppas, 1);
	nvm_generic_to_addr_mode(dev, &rqd);

	ret = dev->ops->set_bb_tbl(dev, &rqd.ppa_addr, rqd.nr_ppas, type);
	nvm_free_rqd_ppalist(dev, &rqd);
	if (ret) {
		pr_err("nvm: sysblk failed bb mark\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(nvm_set_bb_tbl);

int nvm_set_tgt_bb_tbl(struct nvm_tgt_dev *tgt_dev, struct ppa_addr *ppas,
		       int nr_ppas, int type)
{
	struct nvm_dev *dev = tgt_dev->parent;
	struct nvm_rq rqd;
	int ret;

	if (nr_ppas > dev->ops->max_phys_sect) {
		pr_err("nvm: unable to update all blocks atomically\n");
		return -EINVAL;
	}

	memset(&rqd, 0, sizeof(struct nvm_rq));

	nvm_set_rqd_ppalist(dev, &rqd, ppas, nr_ppas, 1);
	nvm_tgt_generic_to_addr_mode(tgt_dev, &rqd);

	ret = dev->ops->set_bb_tbl(dev, &rqd.ppa_addr, rqd.nr_ppas, type);
	nvm_free_rqd_ppalist(dev, &rqd);
	if (ret) {
		pr_err("nvm: sysblk failed bb mark\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(nvm_set_tgt_bb_tbl);

int nvm_max_phys_sects(struct nvm_tgt_dev *tgt_dev)
{
	struct nvm_dev *dev = tgt_dev->parent;

	return dev->ops->max_phys_sect;
}
EXPORT_SYMBOL(nvm_max_phys_sects);

int nvm_submit_io(struct nvm_tgt_dev *tgt_dev, struct nvm_rq *rqd)
{
	struct nvm_dev *dev = tgt_dev->parent;

	return dev->mt->submit_io(tgt_dev, rqd);
}
EXPORT_SYMBOL(nvm_submit_io);

int nvm_erase_blk(struct nvm_tgt_dev *tgt_dev, struct ppa_addr *p, int flags)
{
	struct nvm_dev *dev = tgt_dev->parent;

	return dev->mt->erase_blk(tgt_dev, p, flags);
}
EXPORT_SYMBOL(nvm_erase_blk);

int nvm_get_l2p_tbl(struct nvm_tgt_dev *tgt_dev, u64 slba, u32 nlb,
		    nvm_l2p_update_fn *update_l2p, void *priv)
{
	struct nvm_dev *dev = tgt_dev->parent;

	if (!dev->ops->get_l2p_tbl)
		return 0;

	return dev->ops->get_l2p_tbl(dev, slba, nlb, update_l2p, priv);
}
EXPORT_SYMBOL(nvm_get_l2p_tbl);

int nvm_get_area(struct nvm_tgt_dev *tgt_dev, sector_t *lba, sector_t len)
{
	struct nvm_dev *dev = tgt_dev->parent;

	return dev->mt->get_area(dev, lba, len);
}
EXPORT_SYMBOL(nvm_get_area);

void nvm_put_area(struct nvm_tgt_dev *tgt_dev, sector_t lba)
{
	struct nvm_dev *dev = tgt_dev->parent;

	dev->mt->put_area(dev, lba);
}
EXPORT_SYMBOL(nvm_put_area);

void nvm_addr_to_generic_mode(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	int i;

	if (rqd->nr_ppas > 1) {
		for (i = 0; i < rqd->nr_ppas; i++)
			rqd->ppa_list[i] = dev_to_generic_addr(dev,
							rqd->ppa_list[i]);
	} else {
		rqd->ppa_addr = dev_to_generic_addr(dev, rqd->ppa_addr);
	}
}
EXPORT_SYMBOL(nvm_addr_to_generic_mode);

void nvm_generic_to_addr_mode(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	int i;

	if (rqd->nr_ppas > 1) {
		for (i = 0; i < rqd->nr_ppas; i++)
			rqd->ppa_list[i] = generic_to_dev_addr(dev,
							rqd->ppa_list[i]);
	} else {
		rqd->ppa_addr = generic_to_dev_addr(dev, rqd->ppa_addr);
	}
}
EXPORT_SYMBOL(nvm_generic_to_addr_mode);

int nvm_set_rqd_ppalist(struct nvm_dev *dev, struct nvm_rq *rqd,
			const struct ppa_addr *ppas, int nr_ppas, int vblk)
{
	struct nvm_geo *geo = &dev->geo;
	int i, plane_cnt, pl_idx;
	struct ppa_addr ppa;

	if ((!vblk || geo->plane_mode == NVM_PLANE_SINGLE) && nr_ppas == 1) {
		rqd->nr_ppas = nr_ppas;
		rqd->ppa_addr = ppas[0];

		return 0;
	}

	rqd->nr_ppas = nr_ppas;
	rqd->ppa_list = nvm_dev_dma_alloc(dev, GFP_KERNEL, &rqd->dma_ppa_list);
	if (!rqd->ppa_list) {
		pr_err("nvm: failed to allocate dma memory\n");
		return -ENOMEM;
	}

	if (!vblk) {
		for (i = 0; i < nr_ppas; i++)
			rqd->ppa_list[i] = ppas[i];
	} else {
		plane_cnt = geo->plane_mode;
		rqd->nr_ppas *= plane_cnt;

		for (i = 0; i < nr_ppas; i++) {
			for (pl_idx = 0; pl_idx < plane_cnt; pl_idx++) {
				ppa = ppas[i];
				ppa.g.pl = pl_idx;
				rqd->ppa_list[(pl_idx * nr_ppas) + i] = ppa;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(nvm_set_rqd_ppalist);

void nvm_free_rqd_ppalist(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	if (!rqd->ppa_list)
		return;

	nvm_dev_dma_free(dev, rqd->ppa_list, rqd->dma_ppa_list);
}
EXPORT_SYMBOL(nvm_free_rqd_ppalist);

int nvm_erase_ppa(struct nvm_dev *dev, struct ppa_addr *ppas, int nr_ppas,
								int flags)
{
	struct nvm_rq rqd;
	int ret;

	if (!dev->ops->erase_block)
		return 0;

	memset(&rqd, 0, sizeof(struct nvm_rq));

	ret = nvm_set_rqd_ppalist(dev, &rqd, ppas, nr_ppas, 1);
	if (ret)
		return ret;

	nvm_generic_to_addr_mode(dev, &rqd);

	rqd.flags = flags;

	ret = dev->ops->erase_block(dev, &rqd);

	nvm_free_rqd_ppalist(dev, &rqd);

	return ret;
}
EXPORT_SYMBOL(nvm_erase_ppa);

void nvm_end_io(struct nvm_rq *rqd, int error)
{
	rqd->error = error;
	rqd->end_io(rqd);
}
EXPORT_SYMBOL(nvm_end_io);

static void nvm_end_io_sync(struct nvm_rq *rqd)
{
	struct completion *waiting = rqd->wait;

	rqd->wait = NULL;

	complete(waiting);
}

static int __nvm_submit_ppa(struct nvm_dev *dev, struct nvm_rq *rqd, int opcode,
						int flags, void *buf, int len)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	struct bio *bio;
	int ret;
	unsigned long hang_check;

	bio = bio_map_kern(dev->q, buf, len, GFP_KERNEL);
	if (IS_ERR_OR_NULL(bio))
		return -ENOMEM;

	nvm_generic_to_addr_mode(dev, rqd);

	rqd->dev = NULL;
	rqd->opcode = opcode;
	rqd->flags = flags;
	rqd->bio = bio;
	rqd->wait = &wait;
	rqd->end_io = nvm_end_io_sync;

	ret = dev->ops->submit_io(dev, rqd);
	if (ret) {
		bio_put(bio);
		return ret;
	}

	/* Prevent hang_check timer from firing at us during very long I/O */
	hang_check = sysctl_hung_task_timeout_secs;
	if (hang_check)
		while (!wait_for_completion_io_timeout(&wait,
							hang_check * (HZ/2)))
			;
	else
		wait_for_completion_io(&wait);

	return rqd->error;
}

/**
 * nvm_submit_ppa_list - submit user-defined ppa list to device. The user must
 *			 take to free ppa list if necessary.
 * @dev:	device
 * @ppa_list:	user created ppa_list
 * @nr_ppas:	length of ppa_list
 * @opcode:	device opcode
 * @flags:	device flags
 * @buf:	data buffer
 * @len:	data buffer length
 */
int nvm_submit_ppa_list(struct nvm_dev *dev, struct ppa_addr *ppa_list,
			int nr_ppas, int opcode, int flags, void *buf, int len)
{
	struct nvm_rq rqd;

	if (dev->ops->max_phys_sect < nr_ppas)
		return -EINVAL;

	memset(&rqd, 0, sizeof(struct nvm_rq));

	rqd.nr_ppas = nr_ppas;
	if (nr_ppas > 1)
		rqd.ppa_list = ppa_list;
	else
		rqd.ppa_addr = ppa_list[0];

	return __nvm_submit_ppa(dev, &rqd, opcode, flags, buf, len);
}
EXPORT_SYMBOL(nvm_submit_ppa_list);

/**
 * nvm_submit_ppa - submit PPAs to device. PPAs will automatically be unfolded
 *		    as single, dual, quad plane PPAs depending on device type.
 * @dev:	device
 * @ppa:	user created ppa_list
 * @nr_ppas:	length of ppa_list
 * @opcode:	device opcode
 * @flags:	device flags
 * @buf:	data buffer
 * @len:	data buffer length
 */
int nvm_submit_ppa(struct nvm_dev *dev, struct ppa_addr *ppa, int nr_ppas,
				int opcode, int flags, void *buf, int len)
{
	struct nvm_rq rqd;
	int ret;

	memset(&rqd, 0, sizeof(struct nvm_rq));
	ret = nvm_set_rqd_ppalist(dev, &rqd, ppa, nr_ppas, 1);
	if (ret)
		return ret;

	ret = __nvm_submit_ppa(dev, &rqd, opcode, flags, buf, len);

	nvm_free_rqd_ppalist(dev, &rqd);

	return ret;
}
EXPORT_SYMBOL(nvm_submit_ppa);

/*
 * folds a bad block list from its plane representation to its virtual
 * block representation. The fold is done in place and reduced size is
 * returned.
 *
 * If any of the planes status are bad or grown bad block, the virtual block
 * is marked bad. If not bad, the first plane state acts as the block state.
 */
int nvm_bb_tbl_fold(struct nvm_dev *dev, u8 *blks, int nr_blks)
{
	struct nvm_geo *geo = &dev->geo;
	int blk, offset, pl, blktype;

	if (nr_blks != geo->blks_per_lun * geo->plane_mode)
		return -EINVAL;

	for (blk = 0; blk < geo->blks_per_lun; blk++) {
		offset = blk * geo->plane_mode;
		blktype = blks[offset];

		/* Bad blocks on any planes take precedence over other types */
		for (pl = 0; pl < geo->plane_mode; pl++) {
			if (blks[offset + pl] &
					(NVM_BLK_T_BAD|NVM_BLK_T_GRWN_BAD)) {
				blktype = blks[offset + pl];
				break;
			}
		}

		blks[blk] = blktype;
	}

	return geo->blks_per_lun;
}
EXPORT_SYMBOL(nvm_bb_tbl_fold);

int nvm_get_bb_tbl(struct nvm_dev *dev, struct ppa_addr ppa, u8 *blks)
{
	ppa = generic_to_dev_addr(dev, ppa);

	return dev->ops->get_bb_tbl(dev, ppa, blks);
}
EXPORT_SYMBOL(nvm_get_bb_tbl);

int nvm_get_tgt_bb_tbl(struct nvm_tgt_dev *tgt_dev, struct ppa_addr ppa,
		       u8 *blks)
{
	struct nvm_dev *dev = tgt_dev->parent;

	ppa = dev->mt->trans_ppa(tgt_dev, ppa, TRANS_TGT_TO_DEV);
	return nvm_get_bb_tbl(dev, ppa, blks);
}
EXPORT_SYMBOL(nvm_get_tgt_bb_tbl);

static int nvm_init_slc_tbl(struct nvm_dev *dev, struct nvm_id_group *grp)
{
	struct nvm_geo *geo = &dev->geo;
	int i;

	dev->lps_per_blk = geo->pgs_per_blk;
	dev->lptbl = kcalloc(dev->lps_per_blk, sizeof(int), GFP_KERNEL);
	if (!dev->lptbl)
		return -ENOMEM;

	/* Just a linear array */
	for (i = 0; i < dev->lps_per_blk; i++)
		dev->lptbl[i] = i;

	return 0;
}

static int nvm_init_mlc_tbl(struct nvm_dev *dev, struct nvm_id_group *grp)
{
	int i, p;
	struct nvm_id_lp_mlc *mlc = &grp->lptbl.mlc;

	if (!mlc->num_pairs)
		return 0;

	dev->lps_per_blk = mlc->num_pairs;
	dev->lptbl = kcalloc(dev->lps_per_blk, sizeof(int), GFP_KERNEL);
	if (!dev->lptbl)
		return -ENOMEM;

	/* The lower page table encoding consists of a list of bytes, where each
	 * has a lower and an upper half. The first half byte maintains the
	 * increment value and every value after is an offset added to the
	 * previous incrementation value
	 */
	dev->lptbl[0] = mlc->pairs[0] & 0xF;
	for (i = 1; i < dev->lps_per_blk; i++) {
		p = mlc->pairs[i >> 1];
		if (i & 0x1) /* upper */
			dev->lptbl[i] = dev->lptbl[i - 1] + ((p & 0xF0) >> 4);
		else /* lower */
			dev->lptbl[i] = dev->lptbl[i - 1] + (p & 0xF);
	}

	return 0;
}

static int nvm_core_init(struct nvm_dev *dev)
{
	struct nvm_id *id = &dev->identity;
	struct nvm_id_group *grp = &id->groups[0];
	struct nvm_geo *geo = &dev->geo;
	int ret;

	/* Whole device values */
	geo->nr_chnls = grp->num_ch;
	geo->luns_per_chnl = grp->num_lun;

	/* Generic device values */
	geo->pgs_per_blk = grp->num_pg;
	geo->blks_per_lun = grp->num_blk;
	geo->nr_planes = grp->num_pln;
	geo->fpg_size = grp->fpg_sz;
	geo->pfpg_size = grp->fpg_sz * grp->num_pln;
	geo->sec_size = grp->csecs;
	geo->oob_size = grp->sos;
	geo->sec_per_pg = grp->fpg_sz / grp->csecs;
	geo->mccap = grp->mccap;
	memcpy(&geo->ppaf, &id->ppaf, sizeof(struct nvm_addr_format));

	geo->plane_mode = NVM_PLANE_SINGLE;
	geo->max_rq_size = dev->ops->max_phys_sect * geo->sec_size;

	if (grp->mpos & 0x020202)
		geo->plane_mode = NVM_PLANE_DOUBLE;
	if (grp->mpos & 0x040404)
		geo->plane_mode = NVM_PLANE_QUAD;

	if (grp->mtype != 0) {
		pr_err("nvm: memory type not supported\n");
		return -EINVAL;
	}

	/* calculated values */
	geo->sec_per_pl = geo->sec_per_pg * geo->nr_planes;
	geo->sec_per_blk = geo->sec_per_pl * geo->pgs_per_blk;
	geo->sec_per_lun = geo->sec_per_blk * geo->blks_per_lun;
	geo->nr_luns = geo->luns_per_chnl * geo->nr_chnls;

	dev->total_secs = geo->nr_luns * geo->sec_per_lun;
	dev->lun_map = kcalloc(BITS_TO_LONGS(geo->nr_luns),
					sizeof(unsigned long), GFP_KERNEL);
	if (!dev->lun_map)
		return -ENOMEM;

	switch (grp->fmtype) {
	case NVM_ID_FMTYPE_SLC:
		if (nvm_init_slc_tbl(dev, grp)) {
			ret = -ENOMEM;
			goto err_fmtype;
		}
		break;
	case NVM_ID_FMTYPE_MLC:
		if (nvm_init_mlc_tbl(dev, grp)) {
			ret = -ENOMEM;
			goto err_fmtype;
		}
		break;
	default:
		pr_err("nvm: flash type not supported\n");
		ret = -EINVAL;
		goto err_fmtype;
	}

	mutex_init(&dev->mlock);
	spin_lock_init(&dev->lock);

	blk_queue_logical_block_size(dev->q, geo->sec_size);

	return 0;
err_fmtype:
	kfree(dev->lun_map);
	return ret;
}

static void nvm_free_mgr(struct nvm_dev *dev)
{
	if (!dev->mt)
		return;

	dev->mt->unregister_mgr(dev);
	dev->mt = NULL;
}

void nvm_free(struct nvm_dev *dev)
{
	if (!dev)
		return;

	nvm_free_mgr(dev);

	if (dev->dma_pool)
		dev->ops->destroy_dma_pool(dev->dma_pool);

	kfree(dev->lptbl);
	kfree(dev->lun_map);
	kfree(dev);
}

static int nvm_init(struct nvm_dev *dev)
{
	struct nvm_geo *geo = &dev->geo;
	int ret = -EINVAL;

	if (!dev->q || !dev->ops)
		return ret;

	if (dev->ops->identity(dev, &dev->identity)) {
		pr_err("nvm: device could not be identified\n");
		goto err;
	}

	pr_debug("nvm: ver:%x nvm_vendor:%x groups:%u\n",
			dev->identity.ver_id, dev->identity.vmnt,
							dev->identity.cgrps);

	if (dev->identity.ver_id != 1) {
		pr_err("nvm: device not supported by kernel.");
		goto err;
	}

	if (dev->identity.cgrps != 1) {
		pr_err("nvm: only one group configuration supported.");
		goto err;
	}

	ret = nvm_core_init(dev);
	if (ret) {
		pr_err("nvm: could not initialize core structures.\n");
		goto err;
	}

	pr_info("nvm: registered %s [%u/%u/%u/%u/%u/%u]\n",
			dev->name, geo->sec_per_pg, geo->nr_planes,
			geo->pgs_per_blk, geo->blks_per_lun,
			geo->nr_luns, geo->nr_chnls);
	return 0;
err:
	pr_err("nvm: failed to initialize nvm\n");
	return ret;
}

struct nvm_dev *nvm_alloc_dev(int node)
{
	return kzalloc_node(sizeof(struct nvm_dev), GFP_KERNEL, node);
}
EXPORT_SYMBOL(nvm_alloc_dev);

int nvm_register(struct nvm_dev *dev)
{
	int ret;

	ret = nvm_init(dev);
	if (ret)
		goto err_init;

	if (dev->ops->max_phys_sect > 256) {
		pr_info("nvm: max sectors supported is 256.\n");
		ret = -EINVAL;
		goto err_init;
	}

	if (dev->ops->max_phys_sect > 1) {
		dev->dma_pool = dev->ops->create_dma_pool(dev, "ppalist");
		if (!dev->dma_pool) {
			pr_err("nvm: could not create dma pool\n");
			ret = -ENOMEM;
			goto err_init;
		}
	}

	if (dev->identity.cap & NVM_ID_DCAP_BBLKMGMT) {
		ret = nvm_get_sysblock(dev, &dev->sb);
		if (!ret)
			pr_err("nvm: device not initialized.\n");
		else if (ret < 0)
			pr_err("nvm: err (%d) on device initialization\n", ret);
	}

	/* register device with a supported media manager */
	down_write(&nvm_lock);
	if (ret > 0)
		dev->mt = nvm_init_mgr(dev);
	list_add(&dev->devices, &nvm_devices);
	up_write(&nvm_lock);

	return 0;
err_init:
	kfree(dev->lun_map);
	return ret;
}
EXPORT_SYMBOL(nvm_register);

void nvm_unregister(struct nvm_dev *dev)
{
	down_write(&nvm_lock);
	list_del(&dev->devices);
	up_write(&nvm_lock);

	nvm_free(dev);
}
EXPORT_SYMBOL(nvm_unregister);

static int __nvm_configure_create(struct nvm_ioctl_create *create)
{
	struct nvm_dev *dev;
	struct nvm_ioctl_create_simple *s;

	down_write(&nvm_lock);
	dev = nvm_find_nvm_dev(create->dev);
	up_write(&nvm_lock);

	if (!dev) {
		pr_err("nvm: device not found\n");
		return -EINVAL;
	}

	if (!dev->mt) {
		pr_info("nvm: device has no media manager registered.\n");
		return -ENODEV;
	}

	if (create->conf.type != NVM_CONFIG_TYPE_SIMPLE) {
		pr_err("nvm: config type not valid\n");
		return -EINVAL;
	}
	s = &create->conf.s;

	if (s->lun_begin > s->lun_end || s->lun_end > dev->geo.nr_luns) {
		pr_err("nvm: lun out of bound (%u:%u > %u)\n",
			s->lun_begin, s->lun_end, dev->geo.nr_luns);
		return -EINVAL;
	}

	return dev->mt->create_tgt(dev, create);
}

static long nvm_ioctl_info(struct file *file, void __user *arg)
{
	struct nvm_ioctl_info *info;
	struct nvm_tgt_type *tt;
	int tgt_iter = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	info = memdup_user(arg, sizeof(struct nvm_ioctl_info));
	if (IS_ERR(info))
		return -EFAULT;

	info->version[0] = NVM_VERSION_MAJOR;
	info->version[1] = NVM_VERSION_MINOR;
	info->version[2] = NVM_VERSION_PATCH;

	down_write(&nvm_lock);
	list_for_each_entry(tt, &nvm_tgt_types, list) {
		struct nvm_ioctl_info_tgt *tgt = &info->tgts[tgt_iter];

		tgt->version[0] = tt->version[0];
		tgt->version[1] = tt->version[1];
		tgt->version[2] = tt->version[2];
		strncpy(tgt->tgtname, tt->name, NVM_TTYPE_NAME_MAX);

		tgt_iter++;
	}

	info->tgtsize = tgt_iter;
	up_write(&nvm_lock);

	if (copy_to_user(arg, info, sizeof(struct nvm_ioctl_info))) {
		kfree(info);
		return -EFAULT;
	}

	kfree(info);
	return 0;
}

static long nvm_ioctl_get_devices(struct file *file, void __user *arg)
{
	struct nvm_ioctl_get_devices *devices;
	struct nvm_dev *dev;
	int i = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	devices = kzalloc(sizeof(struct nvm_ioctl_get_devices), GFP_KERNEL);
	if (!devices)
		return -ENOMEM;

	down_write(&nvm_lock);
	list_for_each_entry(dev, &nvm_devices, devices) {
		struct nvm_ioctl_device_info *info = &devices->info[i];

		sprintf(info->devname, "%s", dev->name);
		if (dev->mt) {
			info->bmversion[0] = dev->mt->version[0];
			info->bmversion[1] = dev->mt->version[1];
			info->bmversion[2] = dev->mt->version[2];
			sprintf(info->bmname, "%s", dev->mt->name);
		} else {
			sprintf(info->bmname, "none");
		}

		i++;
		if (i > 31) {
			pr_err("nvm: max 31 devices can be reported.\n");
			break;
		}
	}
	up_write(&nvm_lock);

	devices->nr_devices = i;

	if (copy_to_user(arg, devices,
			 sizeof(struct nvm_ioctl_get_devices))) {
		kfree(devices);
		return -EFAULT;
	}

	kfree(devices);
	return 0;
}

static long nvm_ioctl_dev_create(struct file *file, void __user *arg)
{
	struct nvm_ioctl_create create;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&create, arg, sizeof(struct nvm_ioctl_create)))
		return -EFAULT;

	create.dev[DISK_NAME_LEN - 1] = '\0';
	create.tgttype[NVM_TTYPE_NAME_MAX - 1] = '\0';
	create.tgtname[DISK_NAME_LEN - 1] = '\0';

	if (create.flags != 0) {
		pr_err("nvm: no flags supported\n");
		return -EINVAL;
	}

	return __nvm_configure_create(&create);
}

static long nvm_ioctl_dev_remove(struct file *file, void __user *arg)
{
	struct nvm_ioctl_remove remove;
	struct nvm_dev *dev;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&remove, arg, sizeof(struct nvm_ioctl_remove)))
		return -EFAULT;

	remove.tgtname[DISK_NAME_LEN - 1] = '\0';

	if (remove.flags != 0) {
		pr_err("nvm: no flags supported\n");
		return -EINVAL;
	}

	list_for_each_entry(dev, &nvm_devices, devices) {
		ret = dev->mt->remove_tgt(dev, &remove);
		if (!ret)
			break;
	}

	return ret;
}

static void nvm_setup_nvm_sb_info(struct nvm_sb_info *info)
{
	info->seqnr = 1;
	info->erase_cnt = 0;
	info->version = 1;
}

static long __nvm_ioctl_dev_init(struct nvm_ioctl_dev_init *init)
{
	struct nvm_dev *dev;
	struct nvm_sb_info info;
	int ret;

	down_write(&nvm_lock);
	dev = nvm_find_nvm_dev(init->dev);
	up_write(&nvm_lock);
	if (!dev) {
		pr_err("nvm: device not found\n");
		return -EINVAL;
	}

	nvm_setup_nvm_sb_info(&info);

	strncpy(info.mmtype, init->mmtype, NVM_MMTYPE_LEN);
	info.fs_ppa.ppa = -1;

	if (dev->identity.cap & NVM_ID_DCAP_BBLKMGMT) {
		ret = nvm_init_sysblock(dev, &info);
		if (ret)
			return ret;
	}

	memcpy(&dev->sb, &info, sizeof(struct nvm_sb_info));

	down_write(&nvm_lock);
	dev->mt = nvm_init_mgr(dev);
	up_write(&nvm_lock);

	return 0;
}

static long nvm_ioctl_dev_init(struct file *file, void __user *arg)
{
	struct nvm_ioctl_dev_init init;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&init, arg, sizeof(struct nvm_ioctl_dev_init)))
		return -EFAULT;

	if (init.flags != 0) {
		pr_err("nvm: no flags supported\n");
		return -EINVAL;
	}

	init.dev[DISK_NAME_LEN - 1] = '\0';

	return __nvm_ioctl_dev_init(&init);
}

static long nvm_ioctl_dev_factory(struct file *file, void __user *arg)
{
	struct nvm_ioctl_dev_factory fact;
	struct nvm_dev *dev;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&fact, arg, sizeof(struct nvm_ioctl_dev_factory)))
		return -EFAULT;

	fact.dev[DISK_NAME_LEN - 1] = '\0';

	if (fact.flags & ~(NVM_FACTORY_NR_BITS - 1))
		return -EINVAL;

	down_write(&nvm_lock);
	dev = nvm_find_nvm_dev(fact.dev);
	up_write(&nvm_lock);
	if (!dev) {
		pr_err("nvm: device not found\n");
		return -EINVAL;
	}

	nvm_free_mgr(dev);

	if (dev->identity.cap & NVM_ID_DCAP_BBLKMGMT)
		return nvm_dev_factory(dev, fact.flags);

	return 0;
}

static long nvm_ctl_ioctl(struct file *file, uint cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case NVM_INFO:
		return nvm_ioctl_info(file, argp);
	case NVM_GET_DEVICES:
		return nvm_ioctl_get_devices(file, argp);
	case NVM_DEV_CREATE:
		return nvm_ioctl_dev_create(file, argp);
	case NVM_DEV_REMOVE:
		return nvm_ioctl_dev_remove(file, argp);
	case NVM_DEV_INIT:
		return nvm_ioctl_dev_init(file, argp);
	case NVM_DEV_FACTORY:
		return nvm_ioctl_dev_factory(file, argp);
	}
	return 0;
}

static const struct file_operations _ctl_fops = {
	.open = nonseekable_open,
	.unlocked_ioctl = nvm_ctl_ioctl,
	.owner = THIS_MODULE,
	.llseek  = noop_llseek,
};

static struct miscdevice _nvm_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "lightnvm",
	.nodename	= "lightnvm/control",
	.fops		= &_ctl_fops,
};
builtin_misc_device(_nvm_misc);
