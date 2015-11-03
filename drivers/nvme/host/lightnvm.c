/*
 * nvme-lightnvm.c - LightNVM NVMe device
 *
 * Copyright (C) 2014-2015 IT University of Copenhagen
 * Initial release: Matias Bjorling <mb@lightnvm.io>
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

#include "nvme.h"

#ifdef CONFIG_NVM

#include <linux/nvme.h>
#include <linux/bitops.h>
#include <linux/lightnvm.h>
#include <linux/vmalloc.h>

enum nvme_nvm_admin_opcode {
	nvme_nvm_admin_identity		= 0xe2,
	nvme_nvm_admin_get_l2p_tbl	= 0xea,
	nvme_nvm_admin_get_bb_tbl	= 0xf2,
	nvme_nvm_admin_set_bb_tbl	= 0xf1,
};

struct nvme_nvm_hb_rw {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2;
	__le64			metadata;
	__le64			prp1;
	__le64			prp2;
	__le64			spba;
	__le16			length;
	__le16			control;
	__le32			dsmgmt;
	__le64			slba;
};

struct nvme_nvm_ph_rw {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2;
	__le64			metadata;
	__le64			prp1;
	__le64			prp2;
	__le64			spba;
	__le16			length;
	__le16			control;
	__le32			dsmgmt;
	__le64			resv;
};

struct nvme_nvm_identity {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd[2];
	__le64			prp1;
	__le64			prp2;
	__le32			chnl_off;
	__u32			rsvd11[5];
};

struct nvme_nvm_l2ptbl {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__le32			cdw2[4];
	__le64			prp1;
	__le64			prp2;
	__le64			slba;
	__le32			nlb;
	__le16			cdw14[6];
};

struct nvme_nvm_bbtbl {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd[2];
	__le64			prp1;
	__le64			prp2;
	__le32			prp1_len;
	__le32			prp2_len;
	__le32			lbb;
	__u32			rsvd11[3];
};

struct nvme_nvm_erase_blk {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd[2];
	__le64			prp1;
	__le64			prp2;
	__le64			spba;
	__le16			length;
	__le16			control;
	__le32			dsmgmt;
	__le64			resv;
};

struct nvme_nvm_command {
	union {
		struct nvme_common_command common;
		struct nvme_nvm_identity identity;
		struct nvme_nvm_hb_rw hb_rw;
		struct nvme_nvm_ph_rw ph_rw;
		struct nvme_nvm_l2ptbl l2p;
		struct nvme_nvm_bbtbl get_bb;
		struct nvme_nvm_bbtbl set_bb;
		struct nvme_nvm_erase_blk erase;
	};
};

struct nvme_nvm_id_group {
	__u8			mtype;
	__u8			fmtype;
	__le16			res16;
	__u8			num_ch;
	__u8			num_lun;
	__u8			num_pln;
	__le16			num_blk;
	__le16			num_pg;
	__le16			fpg_sz;
	__le16			csecs;
	__le16			sos;
	__le32			trdt;
	__le32			trdm;
	__le32			tprt;
	__le32			tprm;
	__le32			tbet;
	__le32			tbem;
	__le32			mpos;
	__le16			cpar;
	__u8			reserved[913];
} __packed;

struct nvme_nvm_addr_format {
	__u8			ch_offset;
	__u8			ch_len;
	__u8			lun_offset;
	__u8			lun_len;
	__u8			pln_offset;
	__u8			pln_len;
	__u8			blk_offset;
	__u8			blk_len;
	__u8			pg_offset;
	__u8			pg_len;
	__u8			sect_offset;
	__u8			sect_len;
	__u8			res[4];
} __packed;

struct nvme_nvm_id {
	__u8			ver_id;
	__u8			vmnt;
	__u8			cgrps;
	__u8			res[5];
	__le32			cap;
	__le32			dom;
	struct nvme_nvm_addr_format ppaf;
	__u8			ppat;
	__u8			resv[223];
	struct nvme_nvm_id_group groups[4];
} __packed;

/*
 * Check we didn't inadvertently grow the command struct
 */
static inline void _nvme_nvm_check_size(void)
{
	BUILD_BUG_ON(sizeof(struct nvme_nvm_identity) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_nvm_hb_rw) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_nvm_ph_rw) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_nvm_bbtbl) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_nvm_l2ptbl) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_nvm_erase_blk) != 64);
	BUILD_BUG_ON(sizeof(struct nvme_nvm_id_group) != 960);
	BUILD_BUG_ON(sizeof(struct nvme_nvm_addr_format) != 128);
	BUILD_BUG_ON(sizeof(struct nvme_nvm_id) != 4096);
}

static int init_grps(struct nvm_id *nvm_id, struct nvme_nvm_id *nvme_nvm_id)
{
	struct nvme_nvm_id_group *src;
	struct nvm_id_group *dst;
	int i, end;

	end = min_t(u32, 4, nvm_id->cgrps);

	for (i = 0; i < end; i++) {
		src = &nvme_nvm_id->groups[i];
		dst = &nvm_id->groups[i];

		dst->mtype = src->mtype;
		dst->fmtype = src->fmtype;
		dst->num_ch = src->num_ch;
		dst->num_lun = src->num_lun;
		dst->num_pln = src->num_pln;

		dst->num_pg = le16_to_cpu(src->num_pg);
		dst->num_blk = le16_to_cpu(src->num_blk);
		dst->fpg_sz = le16_to_cpu(src->fpg_sz);
		dst->csecs = le16_to_cpu(src->csecs);
		dst->sos = le16_to_cpu(src->sos);

		dst->trdt = le32_to_cpu(src->trdt);
		dst->trdm = le32_to_cpu(src->trdm);
		dst->tprt = le32_to_cpu(src->tprt);
		dst->tprm = le32_to_cpu(src->tprm);
		dst->tbet = le32_to_cpu(src->tbet);
		dst->tbem = le32_to_cpu(src->tbem);
		dst->mpos = le32_to_cpu(src->mpos);

		dst->cpar = le16_to_cpu(src->cpar);
	}

	return 0;
}

static int nvme_nvm_identity(struct request_queue *q, struct nvm_id *nvm_id)
{
	struct nvme_ns *ns = q->queuedata;
	struct nvme_nvm_id *nvme_nvm_id;
	struct nvme_nvm_command c = {};
	int ret;

	c.identity.opcode = nvme_nvm_admin_identity;
	c.identity.nsid = cpu_to_le32(ns->ns_id);
	c.identity.chnl_off = 0;

	nvme_nvm_id = kmalloc(sizeof(struct nvme_nvm_id), GFP_KERNEL);
	if (!nvme_nvm_id)
		return -ENOMEM;

	ret = nvme_submit_sync_cmd(q, (struct nvme_command *)&c, nvme_nvm_id,
						sizeof(struct nvme_nvm_id));
	if (ret) {
		ret = -EIO;
		goto out;
	}

	nvm_id->ver_id = nvme_nvm_id->ver_id;
	nvm_id->vmnt = nvme_nvm_id->vmnt;
	nvm_id->cgrps = nvme_nvm_id->cgrps;
	nvm_id->cap = le32_to_cpu(nvme_nvm_id->cap);
	nvm_id->dom = le32_to_cpu(nvme_nvm_id->dom);

	ret = init_grps(nvm_id, nvme_nvm_id);
out:
	kfree(nvme_nvm_id);
	return ret;
}

static int nvme_nvm_get_l2p_tbl(struct request_queue *q, u64 slba, u32 nlb,
				nvm_l2p_update_fn *update_l2p, void *priv)
{
	struct nvme_ns *ns = q->queuedata;
	struct nvme_dev *dev = ns->dev;
	struct nvme_nvm_command c = {};
	u32 len = queue_max_hw_sectors(q) << 9;
	u32 nlb_pr_rq = len / sizeof(u64);
	u64 cmd_slba = slba;
	void *entries;
	int ret = 0;

	c.l2p.opcode = nvme_nvm_admin_get_l2p_tbl;
	c.l2p.nsid = cpu_to_le32(ns->ns_id);
	entries = kmalloc(len, GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

	while (nlb) {
		u32 cmd_nlb = min(nlb_pr_rq, nlb);

		c.l2p.slba = cpu_to_le64(cmd_slba);
		c.l2p.nlb = cpu_to_le32(cmd_nlb);

		ret = nvme_submit_sync_cmd(q, (struct nvme_command *)&c,
								entries, len);
		if (ret) {
			dev_err(dev->dev, "L2P table transfer failed (%d)\n",
									ret);
			ret = -EIO;
			goto out;
		}

		if (update_l2p(cmd_slba, cmd_nlb, entries, priv)) {
			ret = -EINTR;
			goto out;
		}

		cmd_slba += cmd_nlb;
		nlb -= cmd_nlb;
	}

out:
	kfree(entries);
	return ret;
}

static int nvme_nvm_get_bb_tbl(struct request_queue *q, int lunid,
				unsigned int nr_blocks,
				nvm_bb_update_fn *update_bbtbl, void *priv)
{
	struct nvme_ns *ns = q->queuedata;
	struct nvme_dev *dev = ns->dev;
	struct nvme_nvm_command c = {};
	void *bb_bitmap;
	u16 bb_bitmap_size;
	int ret = 0;

	c.get_bb.opcode = nvme_nvm_admin_get_bb_tbl;
	c.get_bb.nsid = cpu_to_le32(ns->ns_id);
	c.get_bb.lbb = cpu_to_le32(lunid);
	bb_bitmap_size = ((nr_blocks >> 15) + 1) * PAGE_SIZE;
	bb_bitmap = kmalloc(bb_bitmap_size, GFP_KERNEL);
	if (!bb_bitmap)
		return -ENOMEM;

	bitmap_zero(bb_bitmap, nr_blocks);

	ret = nvme_submit_sync_cmd(q, (struct nvme_command *)&c, bb_bitmap,
								bb_bitmap_size);
	if (ret) {
		dev_err(dev->dev, "get bad block table failed (%d)\n", ret);
		ret = -EIO;
		goto out;
	}

	ret = update_bbtbl(lunid, bb_bitmap, nr_blocks, priv);
	if (ret) {
		ret = -EINTR;
		goto out;
	}

out:
	kfree(bb_bitmap);
	return ret;
}

static inline void nvme_nvm_rqtocmd(struct request *rq, struct nvm_rq *rqd,
				struct nvme_ns *ns, struct nvme_nvm_command *c)
{
	c->ph_rw.opcode = rqd->opcode;
	c->ph_rw.nsid = cpu_to_le32(ns->ns_id);
	c->ph_rw.spba = cpu_to_le64(rqd->ppa_addr.ppa);
	c->ph_rw.control = cpu_to_le16(rqd->flags);
	c->ph_rw.length = cpu_to_le16(rqd->nr_pages - 1);

	if (rqd->opcode == NVM_OP_HBWRITE || rqd->opcode == NVM_OP_HBREAD)
		c->hb_rw.slba = cpu_to_le64(nvme_block_nr(ns,
						rqd->bio->bi_iter.bi_sector));
}

static void nvme_nvm_end_io(struct request *rq, int error)
{
	struct nvm_rq *rqd = rq->end_io_data;
	struct nvm_dev *dev = rqd->dev;

	if (dev->mt->end_io(rqd, error))
		pr_err("nvme: err status: %x result: %lx\n",
				rq->errors, (unsigned long)rq->special);

	kfree(rq->cmd);
	blk_mq_free_request(rq);
}

static int nvme_nvm_submit_io(struct request_queue *q, struct nvm_rq *rqd)
{
	struct nvme_ns *ns = q->queuedata;
	struct request *rq;
	struct bio *bio = rqd->bio;
	struct nvme_nvm_command *cmd;

	rq = blk_mq_alloc_request(q, bio_rw(bio), GFP_KERNEL, 0);
	if (IS_ERR(rq))
		return -ENOMEM;

	cmd = kzalloc(sizeof(struct nvme_nvm_command), GFP_KERNEL);
	if (!cmd) {
		blk_mq_free_request(rq);
		return -ENOMEM;
	}

	rq->cmd_type = REQ_TYPE_DRV_PRIV;
	rq->ioprio = bio_prio(bio);

	if (bio_has_data(bio))
		rq->nr_phys_segments = bio_phys_segments(q, bio);

	rq->__data_len = bio->bi_iter.bi_size;
	rq->bio = rq->biotail = bio;

	nvme_nvm_rqtocmd(rq, rqd, ns, cmd);

	rq->cmd = (unsigned char *)cmd;
	rq->cmd_len = sizeof(struct nvme_nvm_command);
	rq->special = (void *)0;

	rq->end_io_data = rqd;

	blk_execute_rq_nowait(q, NULL, rq, 0, nvme_nvm_end_io);

	return 0;
}

static int nvme_nvm_erase_block(struct request_queue *q, struct nvm_rq *rqd)
{
	struct nvme_ns *ns = q->queuedata;
	struct nvme_nvm_command c = {};

	c.erase.opcode = NVM_OP_ERASE;
	c.erase.nsid = cpu_to_le32(ns->ns_id);
	c.erase.spba = cpu_to_le64(rqd->ppa_addr.ppa);
	c.erase.length = cpu_to_le16(rqd->nr_pages - 1);

	return nvme_submit_sync_cmd(q, (struct nvme_command *)&c, NULL, 0);
}

static void *nvme_nvm_create_dma_pool(struct request_queue *q, char *name)
{
	struct nvme_ns *ns = q->queuedata;
	struct nvme_dev *dev = ns->dev;

	return dma_pool_create(name, dev->dev, PAGE_SIZE, PAGE_SIZE, 0);
}

static void nvme_nvm_destroy_dma_pool(void *pool)
{
	struct dma_pool *dma_pool = pool;

	dma_pool_destroy(dma_pool);
}

static void *nvme_nvm_dev_dma_alloc(struct request_queue *q, void *pool,
				    gfp_t mem_flags, dma_addr_t *dma_handler)
{
	return dma_pool_alloc(pool, mem_flags, dma_handler);
}

static void nvme_nvm_dev_dma_free(void *pool, void *ppa_list,
							dma_addr_t dma_handler)
{
	dma_pool_free(pool, ppa_list, dma_handler);
}

static struct nvm_dev_ops nvme_nvm_dev_ops = {
	.identity		= nvme_nvm_identity,

	.get_l2p_tbl		= nvme_nvm_get_l2p_tbl,

	.get_bb_tbl		= nvme_nvm_get_bb_tbl,

	.submit_io		= nvme_nvm_submit_io,
	.erase_block		= nvme_nvm_erase_block,

	.create_dma_pool	= nvme_nvm_create_dma_pool,
	.destroy_dma_pool	= nvme_nvm_destroy_dma_pool,
	.dev_dma_alloc		= nvme_nvm_dev_dma_alloc,
	.dev_dma_free		= nvme_nvm_dev_dma_free,

	.max_phys_sect		= 64,
};

int nvme_nvm_register(struct request_queue *q, char *disk_name)
{
	return nvm_register(q, disk_name, &nvme_nvm_dev_ops);
}

void nvme_nvm_unregister(struct request_queue *q, char *disk_name)
{
	nvm_unregister(disk_name);
}

int nvme_nvm_ns_supported(struct nvme_ns *ns, struct nvme_id_ns *id)
{
	struct nvme_dev *dev = ns->dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);

	/* QEMU NVMe simulator - PCI ID + Vendor specific bit */
	if (pdev->vendor == PCI_VENDOR_ID_INTEL && pdev->device == 0x5845 &&
							id->vs[0] == 0x1)
		return 1;

	/* CNEX Labs - PCI ID + Vendor specific bit */
	if (pdev->vendor == 0x1d1d && pdev->device == 0x2807 &&
							id->vs[0] == 0x1)
		return 1;

	return 0;
}
#else
int nvme_nvm_register(struct request_queue *q, char *disk_name)
{
	return 0;
}
void nvme_nvm_unregister(struct request_queue *q, char *disk_name) {};
int nvme_nvm_ns_supported(struct nvme_ns *ns, struct nvme_id_ns *id)
{
	return 0;
}
#endif /* CONFIG_NVM */
