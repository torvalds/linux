// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#include <linux/module.h>
#include <linux/printk.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_user_verbs.h>
#include <ionic_api.h>

#include "ionic_fw.h"
#include "ionic_ibdev.h"

#define ionic_set_ecn(tos)   (((tos) | 2u) & ~1u)
#define ionic_clear_ecn(tos)  ((tos) & ~3u)

static int ionic_validate_qdesc(struct ionic_qdesc *q)
{
	if (!q->addr || !q->size || !q->mask ||
	    !q->depth_log2 || !q->stride_log2)
		return -EINVAL;

	if (q->addr & (PAGE_SIZE - 1))
		return -EINVAL;

	if (q->mask != BIT(q->depth_log2) - 1)
		return -EINVAL;

	if (q->size < BIT_ULL(q->depth_log2 + q->stride_log2))
		return -EINVAL;

	return 0;
}

static u32 ionic_get_eqid(struct ionic_ibdev *dev, u32 comp_vector, u8 udma_idx)
{
	/* EQ per vector per udma, and the first eqs reserved for async events.
	 * The rest of the vectors can be requested for completions.
	 */
	u32 comp_vec_count = dev->lif_cfg.eq_count / dev->lif_cfg.udma_count - 1;

	return (comp_vector % comp_vec_count + 1) * dev->lif_cfg.udma_count + udma_idx;
}

static int ionic_get_cqid(struct ionic_ibdev *dev, u32 *cqid, u8 udma_idx)
{
	unsigned int size, base, bound;
	int rc;

	size = dev->lif_cfg.cq_count / dev->lif_cfg.udma_count;
	base = size * udma_idx;
	bound = base + size;

	rc = ionic_resid_get_shared(&dev->inuse_cqid, base, bound);
	if (rc >= 0) {
		/* cq_base is zero or a multiple of two queue groups */
		*cqid = dev->lif_cfg.cq_base +
			ionic_bitid_to_qid(rc, dev->lif_cfg.udma_qgrp_shift,
					   dev->half_cqid_udma_shift);

		rc = 0;
	}

	return rc;
}

static void ionic_put_cqid(struct ionic_ibdev *dev, u32 cqid)
{
	u32 bitid = ionic_qid_to_bitid(cqid - dev->lif_cfg.cq_base,
				       dev->lif_cfg.udma_qgrp_shift,
				       dev->half_cqid_udma_shift);

	ionic_resid_put(&dev->inuse_cqid, bitid);
}

int ionic_create_cq_common(struct ionic_vcq *vcq,
			   struct ionic_tbl_buf *buf,
			   const struct ib_cq_init_attr *attr,
			   struct ionic_ctx *ctx,
			   struct ib_udata *udata,
			   struct ionic_qdesc *req_cq,
			   __u32 *resp_cqid,
			   int udma_idx)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(vcq->ibcq.device);
	struct ionic_cq *cq = &vcq->cq[udma_idx];
	void *entry;
	int rc;

	cq->vcq = vcq;

	if (attr->cqe < 1 || attr->cqe + IONIC_CQ_GRACE > 0xffff) {
		rc = -EINVAL;
		goto err_args;
	}

	rc = ionic_get_cqid(dev, &cq->cqid, udma_idx);
	if (rc)
		goto err_args;

	cq->eqid = ionic_get_eqid(dev, attr->comp_vector, udma_idx);

	spin_lock_init(&cq->lock);
	INIT_LIST_HEAD(&cq->poll_sq);
	INIT_LIST_HEAD(&cq->flush_sq);
	INIT_LIST_HEAD(&cq->flush_rq);

	if (udata) {
		rc = ionic_validate_qdesc(req_cq);
		if (rc)
			goto err_qdesc;

		cq->umem = ib_umem_get(&dev->ibdev, req_cq->addr, req_cq->size,
				       IB_ACCESS_LOCAL_WRITE);
		if (IS_ERR(cq->umem)) {
			rc = PTR_ERR(cq->umem);
			goto err_qdesc;
		}

		cq->q.ptr = NULL;
		cq->q.size = req_cq->size;
		cq->q.mask = req_cq->mask;
		cq->q.depth_log2 = req_cq->depth_log2;
		cq->q.stride_log2 = req_cq->stride_log2;

		*resp_cqid = cq->cqid;
	} else {
		rc = ionic_queue_init(&cq->q, dev->lif_cfg.hwdev,
				      attr->cqe + IONIC_CQ_GRACE,
				      sizeof(struct ionic_v1_cqe));
		if (rc)
			goto err_q_init;

		ionic_queue_dbell_init(&cq->q, cq->cqid);
		cq->color = true;
		cq->credit = cq->q.mask;
	}

	rc = ionic_pgtbl_init(dev, buf, cq->umem, cq->q.dma, 1, PAGE_SIZE);
	if (rc)
		goto err_pgtbl_init;

	init_completion(&cq->cq_rel_comp);
	kref_init(&cq->cq_kref);

	entry = xa_store_irq(&dev->cq_tbl, cq->cqid, cq, GFP_KERNEL);
	if (entry) {
		if (!xa_is_err(entry))
			rc = -EINVAL;
		else
			rc = xa_err(entry);

		goto err_xa;
	}

	return 0;

err_xa:
	ionic_pgtbl_unbuf(dev, buf);
err_pgtbl_init:
	if (!udata)
		ionic_queue_destroy(&cq->q, dev->lif_cfg.hwdev);
err_q_init:
	if (cq->umem)
		ib_umem_release(cq->umem);
err_qdesc:
	ionic_put_cqid(dev, cq->cqid);
err_args:
	cq->vcq = NULL;

	return rc;
}

void ionic_destroy_cq_common(struct ionic_ibdev *dev, struct ionic_cq *cq)
{
	if (!cq->vcq)
		return;

	xa_erase_irq(&dev->cq_tbl, cq->cqid);

	kref_put(&cq->cq_kref, ionic_cq_complete);
	wait_for_completion(&cq->cq_rel_comp);

	if (cq->umem)
		ib_umem_release(cq->umem);
	else
		ionic_queue_destroy(&cq->q, dev->lif_cfg.hwdev);

	ionic_put_cqid(dev, cq->cqid);

	cq->vcq = NULL;
}

static int ionic_validate_qdesc_zero(struct ionic_qdesc *q)
{
	if (q->addr || q->size || q->mask || q->depth_log2 || q->stride_log2)
		return -EINVAL;

	return 0;
}

static int ionic_get_pdid(struct ionic_ibdev *dev, u32 *pdid)
{
	int rc;

	rc = ionic_resid_get(&dev->inuse_pdid);
	if (rc < 0)
		return rc;

	*pdid = rc;
	return 0;
}

static int ionic_get_ahid(struct ionic_ibdev *dev, u32 *ahid)
{
	int rc;

	rc = ionic_resid_get(&dev->inuse_ahid);
	if (rc < 0)
		return rc;

	*ahid = rc;
	return 0;
}

static int ionic_get_mrid(struct ionic_ibdev *dev, u32 *mrid)
{
	int rc;

	/* wrap to 1, skip reserved lkey */
	rc = ionic_resid_get_shared(&dev->inuse_mrid, 1,
				    dev->inuse_mrid.inuse_size);
	if (rc < 0)
		return rc;

	*mrid = ionic_mrid(rc, dev->next_mrkey++);
	return 0;
}

static int ionic_get_gsi_qpid(struct ionic_ibdev *dev, u32 *qpid)
{
	int rc = 0;

	rc = ionic_resid_get_shared(&dev->inuse_qpid, IB_QPT_GSI, IB_QPT_GSI + 1);
	if (rc < 0)
		return rc;

	*qpid = IB_QPT_GSI;
	return 0;
}

static int ionic_get_qpid(struct ionic_ibdev *dev, u32 *qpid,
			  u8 *udma_idx, u8 udma_mask)
{
	unsigned int size, base, bound;
	int udma_i, udma_x, udma_ix;
	int rc = -EINVAL;

	udma_x = dev->next_qpid_udma_idx;

	dev->next_qpid_udma_idx ^= dev->lif_cfg.udma_count - 1;

	for (udma_i = 0; udma_i < dev->lif_cfg.udma_count; ++udma_i) {
		udma_ix = udma_i ^ udma_x;

		if (!(udma_mask & BIT(udma_ix)))
			continue;

		size = dev->lif_cfg.qp_count / dev->lif_cfg.udma_count;
		base = size * udma_ix;
		bound = base + size;

		/* skip reserved SMI and GSI qpids in group zero */
		if (!base)
			base = 2;

		rc = ionic_resid_get_shared(&dev->inuse_qpid, base, bound);
		if (rc >= 0) {
			*qpid = ionic_bitid_to_qid(rc,
						   dev->lif_cfg.udma_qgrp_shift,
						   dev->half_qpid_udma_shift);
			*udma_idx = udma_ix;

			rc = 0;
			break;
		}
	}

	return rc;
}

static int ionic_get_dbid(struct ionic_ibdev *dev, u32 *dbid, phys_addr_t *addr)
{
	int rc, dbpage_num;

	/* wrap to 1, skip kernel reserved */
	rc = ionic_resid_get_shared(&dev->inuse_dbid, 1,
				    dev->inuse_dbid.inuse_size);
	if (rc < 0)
		return rc;

	dbpage_num = (dev->lif_cfg.lif_hw_index * dev->lif_cfg.dbid_count) + rc;
	*addr = dev->lif_cfg.db_phys + ((phys_addr_t)dbpage_num << PAGE_SHIFT);

	*dbid = rc;

	return 0;
}

static void ionic_put_pdid(struct ionic_ibdev *dev, u32 pdid)
{
	ionic_resid_put(&dev->inuse_pdid, pdid);
}

static void ionic_put_ahid(struct ionic_ibdev *dev, u32 ahid)
{
	ionic_resid_put(&dev->inuse_ahid, ahid);
}

static void ionic_put_mrid(struct ionic_ibdev *dev, u32 mrid)
{
	ionic_resid_put(&dev->inuse_mrid, ionic_mrid_index(mrid));
}

static void ionic_put_qpid(struct ionic_ibdev *dev, u32 qpid)
{
	u32 bitid = ionic_qid_to_bitid(qpid,
				       dev->lif_cfg.udma_qgrp_shift,
				       dev->half_qpid_udma_shift);

	ionic_resid_put(&dev->inuse_qpid, bitid);
}

static void ionic_put_dbid(struct ionic_ibdev *dev, u32 dbid)
{
	ionic_resid_put(&dev->inuse_dbid, dbid);
}

static struct rdma_user_mmap_entry*
ionic_mmap_entry_insert(struct ionic_ctx *ctx, unsigned long size,
			unsigned long pfn, u8 mmap_flags, u64 *offset)
{
	struct ionic_mmap_entry *entry;
	int rc;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	entry->size = size;
	entry->pfn = pfn;
	entry->mmap_flags = mmap_flags;

	rc = rdma_user_mmap_entry_insert(&ctx->ibctx, &entry->rdma_entry,
					 entry->size);
	if (rc) {
		kfree(entry);
		return NULL;
	}

	if (offset)
		*offset = rdma_user_mmap_get_offset(&entry->rdma_entry);

	return &entry->rdma_entry;
}

int ionic_alloc_ucontext(struct ib_ucontext *ibctx, struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibctx->device);
	struct ionic_ctx *ctx = to_ionic_ctx(ibctx);
	struct ionic_ctx_resp resp = {};
	struct ionic_ctx_req req;
	phys_addr_t db_phys = 0;
	int rc;

	rc = ib_copy_from_udata(&req, udata, sizeof(req));
	if (rc)
		return rc;

	/* try to allocate dbid for user ctx */
	rc = ionic_get_dbid(dev, &ctx->dbid, &db_phys);
	if (rc < 0)
		return rc;

	ibdev_dbg(&dev->ibdev, "user space dbid %u\n", ctx->dbid);

	ctx->mmap_dbell = ionic_mmap_entry_insert(ctx, PAGE_SIZE,
						  PHYS_PFN(db_phys), 0, NULL);
	if (!ctx->mmap_dbell) {
		rc = -ENOMEM;
		goto err_mmap_dbell;
	}

	resp.page_shift = PAGE_SHIFT;

	resp.dbell_offset = db_phys & ~PAGE_MASK;

	resp.version = dev->lif_cfg.rdma_version;
	resp.qp_opcodes = dev->lif_cfg.qp_opcodes;
	resp.admin_opcodes = dev->lif_cfg.admin_opcodes;

	resp.sq_qtype = dev->lif_cfg.sq_qtype;
	resp.rq_qtype = dev->lif_cfg.rq_qtype;
	resp.cq_qtype = dev->lif_cfg.cq_qtype;
	resp.admin_qtype = dev->lif_cfg.aq_qtype;
	resp.max_stride = dev->lif_cfg.max_stride;
	resp.max_spec = IONIC_SPEC_HIGH;

	resp.udma_count = dev->lif_cfg.udma_count;
	resp.expdb_mask = dev->lif_cfg.expdb_mask;

	if (dev->lif_cfg.sq_expdb)
		resp.expdb_qtypes |= IONIC_EXPDB_SQ;
	if (dev->lif_cfg.rq_expdb)
		resp.expdb_qtypes |= IONIC_EXPDB_RQ;

	rc = ib_copy_to_udata(udata, &resp, sizeof(resp));
	if (rc)
		goto err_resp;

	return 0;

err_resp:
	rdma_user_mmap_entry_remove(ctx->mmap_dbell);
err_mmap_dbell:
	ionic_put_dbid(dev, ctx->dbid);

	return rc;
}

void ionic_dealloc_ucontext(struct ib_ucontext *ibctx)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibctx->device);
	struct ionic_ctx *ctx = to_ionic_ctx(ibctx);

	rdma_user_mmap_entry_remove(ctx->mmap_dbell);
	ionic_put_dbid(dev, ctx->dbid);
}

int ionic_mmap(struct ib_ucontext *ibctx, struct vm_area_struct *vma)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibctx->device);
	struct ionic_ctx *ctx = to_ionic_ctx(ibctx);
	struct rdma_user_mmap_entry *rdma_entry;
	struct ionic_mmap_entry *ionic_entry;
	int rc = 0;

	rdma_entry = rdma_user_mmap_entry_get(&ctx->ibctx, vma);
	if (!rdma_entry) {
		ibdev_dbg(&dev->ibdev, "not found %#lx\n",
			  vma->vm_pgoff << PAGE_SHIFT);
		return -EINVAL;
	}

	ionic_entry = container_of(rdma_entry, struct ionic_mmap_entry,
				   rdma_entry);

	ibdev_dbg(&dev->ibdev, "writecombine? %d\n",
		  ionic_entry->mmap_flags & IONIC_MMAP_WC);
	if (ionic_entry->mmap_flags & IONIC_MMAP_WC)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	ibdev_dbg(&dev->ibdev, "remap st %#lx pf %#lx sz %#lx\n",
		  vma->vm_start, ionic_entry->pfn, ionic_entry->size);
	rc = rdma_user_mmap_io(&ctx->ibctx, vma, ionic_entry->pfn,
			       ionic_entry->size, vma->vm_page_prot,
			       rdma_entry);
	if (rc)
		ibdev_dbg(&dev->ibdev, "remap failed %d\n", rc);

	rdma_user_mmap_entry_put(rdma_entry);
	return rc;
}

void ionic_mmap_free(struct rdma_user_mmap_entry *rdma_entry)
{
	struct ionic_mmap_entry *ionic_entry;

	ionic_entry = container_of(rdma_entry, struct ionic_mmap_entry,
				   rdma_entry);
	kfree(ionic_entry);
}

int ionic_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibpd->device);
	struct ionic_pd *pd = to_ionic_pd(ibpd);

	return ionic_get_pdid(dev, &pd->pdid);
}

int ionic_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibpd->device);
	struct ionic_pd *pd = to_ionic_pd(ibpd);

	ionic_put_pdid(dev, pd->pdid);

	return 0;
}

static int ionic_build_hdr(struct ionic_ibdev *dev,
			   struct ib_ud_header *hdr,
			   const struct rdma_ah_attr *attr,
			   u16 sport, bool want_ecn)
{
	const struct ib_global_route *grh;
	enum rdma_network_type net;
	u16 vlan;
	int rc;

	if (attr->ah_flags != IB_AH_GRH)
		return -EINVAL;
	if (attr->type != RDMA_AH_ATTR_TYPE_ROCE)
		return -EINVAL;

	grh = rdma_ah_read_grh(attr);

	rc = rdma_read_gid_l2_fields(grh->sgid_attr, &vlan, &hdr->eth.smac_h[0]);
	if (rc)
		return rc;

	net = rdma_gid_attr_network_type(grh->sgid_attr);

	rc = ib_ud_header_init(0,	/* no payload */
			       0,	/* no lrh */
			       1,	/* yes eth */
			       vlan != 0xffff,
			       0,	/* no grh */
			       net == RDMA_NETWORK_IPV4 ? 4 : 6,
			       1,	/* yes udp */
			       0,	/* no imm */
			       hdr);
	if (rc)
		return rc;

	ether_addr_copy(hdr->eth.dmac_h, attr->roce.dmac);

	if (net == RDMA_NETWORK_IPV4) {
		hdr->eth.type = cpu_to_be16(ETH_P_IP);
		hdr->ip4.frag_off = cpu_to_be16(0x4000); /* don't fragment */
		hdr->ip4.ttl = grh->hop_limit;
		hdr->ip4.tot_len = cpu_to_be16(0xffff);
		hdr->ip4.saddr =
			*(const __be32 *)(grh->sgid_attr->gid.raw + 12);
		hdr->ip4.daddr = *(const __be32 *)(grh->dgid.raw + 12);

		if (want_ecn)
			hdr->ip4.tos = ionic_set_ecn(grh->traffic_class);
		else
			hdr->ip4.tos = ionic_clear_ecn(grh->traffic_class);
	} else {
		hdr->eth.type = cpu_to_be16(ETH_P_IPV6);
		hdr->grh.flow_label = cpu_to_be32(grh->flow_label);
		hdr->grh.hop_limit = grh->hop_limit;
		hdr->grh.source_gid = grh->sgid_attr->gid;
		hdr->grh.destination_gid = grh->dgid;

		if (want_ecn)
			hdr->grh.traffic_class =
				ionic_set_ecn(grh->traffic_class);
		else
			hdr->grh.traffic_class =
				ionic_clear_ecn(grh->traffic_class);
	}

	if (vlan != 0xffff) {
		vlan |= rdma_ah_get_sl(attr) << VLAN_PRIO_SHIFT;
		hdr->vlan.tag = cpu_to_be16(vlan);
		hdr->vlan.type = hdr->eth.type;
		hdr->eth.type = cpu_to_be16(ETH_P_8021Q);
	}

	hdr->udp.sport = cpu_to_be16(sport);
	hdr->udp.dport = cpu_to_be16(ROCE_V2_UDP_DPORT);

	return 0;
}

static void ionic_set_ah_attr(struct ionic_ibdev *dev,
			      struct rdma_ah_attr *ah_attr,
			      struct ib_ud_header *hdr,
			      int sgid_index)
{
	u32 flow_label;
	u16 vlan = 0;
	u8  tos, ttl;

	if (hdr->vlan_present)
		vlan = be16_to_cpu(hdr->vlan.tag);

	if (hdr->ipv4_present) {
		flow_label = 0;
		ttl = hdr->ip4.ttl;
		tos = hdr->ip4.tos;
		*(__be16 *)(hdr->grh.destination_gid.raw + 10) = cpu_to_be16(0xffff);
		*(__be32 *)(hdr->grh.destination_gid.raw + 12) = hdr->ip4.daddr;
	} else {
		flow_label = be32_to_cpu(hdr->grh.flow_label);
		ttl = hdr->grh.hop_limit;
		tos = hdr->grh.traffic_class;
	}

	memset(ah_attr, 0, sizeof(*ah_attr));
	ah_attr->type = RDMA_AH_ATTR_TYPE_ROCE;
	if (hdr->eth_present)
		ether_addr_copy(ah_attr->roce.dmac, hdr->eth.dmac_h);
	rdma_ah_set_sl(ah_attr, vlan >> VLAN_PRIO_SHIFT);
	rdma_ah_set_port_num(ah_attr, 1);
	rdma_ah_set_grh(ah_attr, NULL, flow_label, sgid_index, ttl, tos);
	rdma_ah_set_dgid_raw(ah_attr, &hdr->grh.destination_gid);
}

static int ionic_create_ah_cmd(struct ionic_ibdev *dev,
			       struct ionic_ah *ah,
			       struct ionic_pd *pd,
			       struct rdma_ah_attr *attr,
			       u32 flags)
{
	struct ionic_admin_wr wr = {
		.work = COMPLETION_INITIALIZER_ONSTACK(wr.work),
		.wqe = {
			.op = IONIC_V1_ADMIN_CREATE_AH,
			.len = cpu_to_le16(IONIC_ADMIN_CREATE_AH_IN_V1_LEN),
			.cmd.create_ah = {
				.pd_id = cpu_to_le32(pd->pdid),
				.dbid_flags = cpu_to_le16(dev->lif_cfg.dbid),
				.id_ver = cpu_to_le32(ah->ahid),
			}
		}
	};
	enum ionic_admin_flags admin_flags = 0;
	dma_addr_t hdr_dma = 0;
	void *hdr_buf;
	gfp_t gfp = GFP_ATOMIC;
	int rc, hdr_len = 0;

	if (dev->lif_cfg.admin_opcodes <= IONIC_V1_ADMIN_CREATE_AH)
		return -EBADRQC;

	if (flags & RDMA_CREATE_AH_SLEEPABLE)
		gfp = GFP_KERNEL;
	else
		admin_flags |= IONIC_ADMIN_F_BUSYWAIT;

	rc = ionic_build_hdr(dev, &ah->hdr, attr, IONIC_ROCE_UDP_SPORT, false);
	if (rc)
		return rc;

	if (ah->hdr.eth.type == cpu_to_be16(ETH_P_8021Q)) {
		if (ah->hdr.vlan.type == cpu_to_be16(ETH_P_IP))
			wr.wqe.cmd.create_ah.csum_profile =
				IONIC_TFP_CSUM_PROF_ETH_QTAG_IPV4_UDP;
		else
			wr.wqe.cmd.create_ah.csum_profile =
				IONIC_TFP_CSUM_PROF_ETH_QTAG_IPV6_UDP;
	} else {
		if (ah->hdr.eth.type == cpu_to_be16(ETH_P_IP))
			wr.wqe.cmd.create_ah.csum_profile =
				IONIC_TFP_CSUM_PROF_ETH_IPV4_UDP;
		else
			wr.wqe.cmd.create_ah.csum_profile =
				IONIC_TFP_CSUM_PROF_ETH_IPV6_UDP;
	}

	ah->sgid_index = rdma_ah_read_grh(attr)->sgid_index;

	hdr_buf = kmalloc(PAGE_SIZE, gfp);
	if (!hdr_buf)
		return -ENOMEM;

	hdr_len = ib_ud_header_pack(&ah->hdr, hdr_buf);
	hdr_len -= IB_BTH_BYTES;
	hdr_len -= IB_DETH_BYTES;
	ibdev_dbg(&dev->ibdev, "roce packet header template\n");
	print_hex_dump_debug("hdr ", DUMP_PREFIX_OFFSET, 16, 1,
			     hdr_buf, hdr_len, true);

	hdr_dma = dma_map_single(dev->lif_cfg.hwdev, hdr_buf, hdr_len,
				 DMA_TO_DEVICE);

	rc = dma_mapping_error(dev->lif_cfg.hwdev, hdr_dma);
	if (rc)
		goto err_dma;

	wr.wqe.cmd.create_ah.dma_addr = cpu_to_le64(hdr_dma);
	wr.wqe.cmd.create_ah.length = cpu_to_le32(hdr_len);

	ionic_admin_post(dev, &wr);
	rc = ionic_admin_wait(dev, &wr, admin_flags);

	dma_unmap_single(dev->lif_cfg.hwdev, hdr_dma, hdr_len,
			 DMA_TO_DEVICE);
err_dma:
	kfree(hdr_buf);

	return rc;
}

static int ionic_destroy_ah_cmd(struct ionic_ibdev *dev, u32 ahid, u32 flags)
{
	struct ionic_admin_wr wr = {
		.work = COMPLETION_INITIALIZER_ONSTACK(wr.work),
		.wqe = {
			.op = IONIC_V1_ADMIN_DESTROY_AH,
			.len = cpu_to_le16(IONIC_ADMIN_DESTROY_AH_IN_V1_LEN),
			.cmd.destroy_ah = {
				.ah_id = cpu_to_le32(ahid),
			},
		}
	};
	enum ionic_admin_flags admin_flags = IONIC_ADMIN_F_TEARDOWN;

	if (dev->lif_cfg.admin_opcodes <= IONIC_V1_ADMIN_DESTROY_AH)
		return -EBADRQC;

	if (!(flags & RDMA_CREATE_AH_SLEEPABLE))
		admin_flags |= IONIC_ADMIN_F_BUSYWAIT;

	ionic_admin_post(dev, &wr);
	ionic_admin_wait(dev, &wr, admin_flags);

	/* No host-memory resource is associated with ah, so it is ok
	 * to "succeed" and complete this destroy ah on the host.
	 */
	return 0;
}

int ionic_create_ah(struct ib_ah *ibah, struct rdma_ah_init_attr *init_attr,
		    struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibah->device);
	struct rdma_ah_attr *attr = init_attr->ah_attr;
	struct ionic_pd *pd = to_ionic_pd(ibah->pd);
	struct ionic_ah *ah = to_ionic_ah(ibah);
	struct ionic_ah_resp resp = {};
	u32 flags = init_attr->flags;
	int rc;

	rc = ionic_get_ahid(dev, &ah->ahid);
	if (rc)
		return rc;

	rc = ionic_create_ah_cmd(dev, ah, pd, attr, flags);
	if (rc)
		goto err_cmd;

	if (udata) {
		resp.ahid = ah->ahid;

		rc = ib_copy_to_udata(udata, &resp, sizeof(resp));
		if (rc)
			goto err_resp;
	}

	return 0;

err_resp:
	ionic_destroy_ah_cmd(dev, ah->ahid, flags);
err_cmd:
	ionic_put_ahid(dev, ah->ahid);
	return rc;
}

int ionic_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibah->device);
	struct ionic_ah *ah = to_ionic_ah(ibah);

	ionic_set_ah_attr(dev, ah_attr, &ah->hdr, ah->sgid_index);

	return 0;
}

int ionic_destroy_ah(struct ib_ah *ibah, u32 flags)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibah->device);
	struct ionic_ah *ah = to_ionic_ah(ibah);
	int rc;

	rc = ionic_destroy_ah_cmd(dev, ah->ahid, flags);
	if (rc)
		return rc;

	ionic_put_ahid(dev, ah->ahid);

	return 0;
}

static int ionic_create_mr_cmd(struct ionic_ibdev *dev,
			       struct ionic_pd *pd,
			       struct ionic_mr *mr,
			       u64 addr,
			       u64 length)
{
	struct ionic_admin_wr wr = {
		.work = COMPLETION_INITIALIZER_ONSTACK(wr.work),
		.wqe = {
			.op = IONIC_V1_ADMIN_CREATE_MR,
			.len = cpu_to_le16(IONIC_ADMIN_CREATE_MR_IN_V1_LEN),
			.cmd.create_mr = {
				.va = cpu_to_le64(addr),
				.length = cpu_to_le64(length),
				.pd_id = cpu_to_le32(pd->pdid),
				.page_size_log2 = mr->buf.page_size_log2,
				.tbl_index = cpu_to_le32(~0),
				.map_count = cpu_to_le32(mr->buf.tbl_pages),
				.dma_addr = ionic_pgtbl_dma(&mr->buf, addr),
				.dbid_flags = cpu_to_le16(mr->flags),
				.id_ver = cpu_to_le32(mr->mrid),
			}
		}
	};
	int rc;

	if (dev->lif_cfg.admin_opcodes <= IONIC_V1_ADMIN_CREATE_MR)
		return -EBADRQC;

	ionic_admin_post(dev, &wr);
	rc = ionic_admin_wait(dev, &wr, 0);
	if (!rc)
		mr->created = true;

	return rc;
}

static int ionic_destroy_mr_cmd(struct ionic_ibdev *dev, u32 mrid)
{
	struct ionic_admin_wr wr = {
		.work = COMPLETION_INITIALIZER_ONSTACK(wr.work),
		.wqe = {
			.op = IONIC_V1_ADMIN_DESTROY_MR,
			.len = cpu_to_le16(IONIC_ADMIN_DESTROY_MR_IN_V1_LEN),
			.cmd.destroy_mr = {
				.mr_id = cpu_to_le32(mrid),
			},
		}
	};

	if (dev->lif_cfg.admin_opcodes <= IONIC_V1_ADMIN_DESTROY_MR)
		return -EBADRQC;

	ionic_admin_post(dev, &wr);

	return ionic_admin_wait(dev, &wr, IONIC_ADMIN_F_TEARDOWN);
}

struct ib_mr *ionic_get_dma_mr(struct ib_pd *ibpd, int access)
{
	struct ionic_pd *pd = to_ionic_pd(ibpd);
	struct ionic_mr *mr;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->ibmr.lkey = IONIC_DMA_LKEY;
	mr->ibmr.rkey = IONIC_DMA_RKEY;

	if (pd)
		pd->flags |= IONIC_QPF_PRIVILEGED;

	return &mr->ibmr;
}

struct ib_mr *ionic_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 length,
				u64 addr, int access, struct ib_dmah *dmah,
				struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibpd->device);
	struct ionic_pd *pd = to_ionic_pd(ibpd);
	struct ionic_mr *mr;
	unsigned long pg_sz;
	int rc;

	if (dmah)
		return ERR_PTR(-EOPNOTSUPP);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	rc = ionic_get_mrid(dev, &mr->mrid);
	if (rc)
		goto err_mrid;

	mr->ibmr.lkey = mr->mrid;
	mr->ibmr.rkey = mr->mrid;
	mr->ibmr.iova = addr;
	mr->ibmr.length = length;

	mr->flags = IONIC_MRF_USER_MR | to_ionic_mr_flags(access);

	mr->umem = ib_umem_get(&dev->ibdev, start, length, access);
	if (IS_ERR(mr->umem)) {
		rc = PTR_ERR(mr->umem);
		goto err_umem;
	}

	pg_sz = ib_umem_find_best_pgsz(mr->umem,
				       dev->lif_cfg.page_size_supported,
				       addr);
	if (!pg_sz) {
		rc = -EINVAL;
		goto err_pgtbl;
	}

	rc = ionic_pgtbl_init(dev, &mr->buf, mr->umem, 0, 1, pg_sz);
	if (rc)
		goto err_pgtbl;

	rc = ionic_create_mr_cmd(dev, pd, mr, addr, length);
	if (rc)
		goto err_cmd;

	ionic_pgtbl_unbuf(dev, &mr->buf);

	return &mr->ibmr;

err_cmd:
	ionic_pgtbl_unbuf(dev, &mr->buf);
err_pgtbl:
	ib_umem_release(mr->umem);
err_umem:
	ionic_put_mrid(dev, mr->mrid);
err_mrid:
	kfree(mr);
	return ERR_PTR(rc);
}

struct ib_mr *ionic_reg_user_mr_dmabuf(struct ib_pd *ibpd, u64 offset,
				       u64 length, u64 addr, int fd, int access,
				       struct ib_dmah *dmah,
				       struct uverbs_attr_bundle *attrs)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibpd->device);
	struct ionic_pd *pd = to_ionic_pd(ibpd);
	struct ib_umem_dmabuf *umem_dmabuf;
	struct ionic_mr *mr;
	u64 pg_sz;
	int rc;

	if (dmah)
		return ERR_PTR(-EOPNOTSUPP);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	rc = ionic_get_mrid(dev, &mr->mrid);
	if (rc)
		goto err_mrid;

	mr->ibmr.lkey = mr->mrid;
	mr->ibmr.rkey = mr->mrid;
	mr->ibmr.iova = addr;
	mr->ibmr.length = length;

	mr->flags = IONIC_MRF_USER_MR | to_ionic_mr_flags(access);

	umem_dmabuf = ib_umem_dmabuf_get_pinned(&dev->ibdev, offset, length,
						fd, access);
	if (IS_ERR(umem_dmabuf)) {
		rc = PTR_ERR(umem_dmabuf);
		goto err_umem;
	}

	mr->umem = &umem_dmabuf->umem;

	pg_sz = ib_umem_find_best_pgsz(mr->umem,
				       dev->lif_cfg.page_size_supported,
				       addr);
	if (!pg_sz) {
		rc = -EINVAL;
		goto err_pgtbl;
	}

	rc = ionic_pgtbl_init(dev, &mr->buf, mr->umem, 0, 1, pg_sz);
	if (rc)
		goto err_pgtbl;

	rc = ionic_create_mr_cmd(dev, pd, mr, addr, length);
	if (rc)
		goto err_cmd;

	ionic_pgtbl_unbuf(dev, &mr->buf);

	return &mr->ibmr;

err_cmd:
	ionic_pgtbl_unbuf(dev, &mr->buf);
err_pgtbl:
	ib_umem_release(mr->umem);
err_umem:
	ionic_put_mrid(dev, mr->mrid);
err_mrid:
	kfree(mr);
	return ERR_PTR(rc);
}

int ionic_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibmr->device);
	struct ionic_mr *mr = to_ionic_mr(ibmr);
	int rc;

	if (!mr->ibmr.lkey)
		goto out;

	if (mr->created) {
		rc = ionic_destroy_mr_cmd(dev, mr->mrid);
		if (rc)
			return rc;
	}

	ionic_pgtbl_unbuf(dev, &mr->buf);

	if (mr->umem)
		ib_umem_release(mr->umem);

	ionic_put_mrid(dev, mr->mrid);

out:
	kfree(mr);

	return 0;
}

struct ib_mr *ionic_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type type,
			     u32 max_sg)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibpd->device);
	struct ionic_pd *pd = to_ionic_pd(ibpd);
	struct ionic_mr *mr;
	int rc;

	if (type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	rc = ionic_get_mrid(dev, &mr->mrid);
	if (rc)
		goto err_mrid;

	mr->ibmr.lkey = mr->mrid;
	mr->ibmr.rkey = mr->mrid;

	mr->flags = IONIC_MRF_PHYS_MR;

	rc = ionic_pgtbl_init(dev, &mr->buf, mr->umem, 0, max_sg, PAGE_SIZE);
	if (rc)
		goto err_pgtbl;

	mr->buf.tbl_pages = 0;

	rc = ionic_create_mr_cmd(dev, pd, mr, 0, 0);
	if (rc)
		goto err_cmd;

	return &mr->ibmr;

err_cmd:
	ionic_pgtbl_unbuf(dev, &mr->buf);
err_pgtbl:
	ionic_put_mrid(dev, mr->mrid);
err_mrid:
	kfree(mr);
	return ERR_PTR(rc);
}

static int ionic_map_mr_page(struct ib_mr *ibmr, u64 dma)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibmr->device);
	struct ionic_mr *mr = to_ionic_mr(ibmr);

	ibdev_dbg(&dev->ibdev, "dma %p\n", (void *)dma);
	return ionic_pgtbl_page(&mr->buf, dma);
}

int ionic_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		    unsigned int *sg_offset)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibmr->device);
	struct ionic_mr *mr = to_ionic_mr(ibmr);
	int rc;

	/* mr must be allocated using ib_alloc_mr() */
	if (unlikely(!mr->buf.tbl_limit))
		return -EINVAL;

	mr->buf.tbl_pages = 0;

	if (mr->buf.tbl_buf)
		dma_sync_single_for_cpu(dev->lif_cfg.hwdev, mr->buf.tbl_dma,
					mr->buf.tbl_size, DMA_TO_DEVICE);

	ibdev_dbg(&dev->ibdev, "sg %p nent %d\n", sg, sg_nents);
	rc = ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, ionic_map_mr_page);

	mr->buf.page_size_log2 = order_base_2(ibmr->page_size);

	if (mr->buf.tbl_buf)
		dma_sync_single_for_device(dev->lif_cfg.hwdev, mr->buf.tbl_dma,
					   mr->buf.tbl_size, DMA_TO_DEVICE);

	return rc;
}

int ionic_alloc_mw(struct ib_mw *ibmw, struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibmw->device);
	struct ionic_pd *pd = to_ionic_pd(ibmw->pd);
	struct ionic_mr *mr = to_ionic_mw(ibmw);
	int rc;

	rc = ionic_get_mrid(dev, &mr->mrid);
	if (rc)
		return rc;

	mr->ibmw.rkey = mr->mrid;

	if (mr->ibmw.type == IB_MW_TYPE_1)
		mr->flags = IONIC_MRF_MW_1;
	else
		mr->flags = IONIC_MRF_MW_2;

	rc = ionic_create_mr_cmd(dev, pd, mr, 0, 0);
	if (rc)
		goto err_cmd;

	return 0;

err_cmd:
	ionic_put_mrid(dev, mr->mrid);
	return rc;
}

int ionic_dealloc_mw(struct ib_mw *ibmw)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibmw->device);
	struct ionic_mr *mr = to_ionic_mw(ibmw);
	int rc;

	rc = ionic_destroy_mr_cmd(dev, mr->mrid);
	if (rc)
		return rc;

	ionic_put_mrid(dev, mr->mrid);

	return 0;
}

static int ionic_create_cq_cmd(struct ionic_ibdev *dev,
			       struct ionic_ctx *ctx,
			       struct ionic_cq *cq,
			       struct ionic_tbl_buf *buf)
{
	const u16 dbid = ionic_ctx_dbid(dev, ctx);
	struct ionic_admin_wr wr = {
		.work = COMPLETION_INITIALIZER_ONSTACK(wr.work),
		.wqe = {
			.op = IONIC_V1_ADMIN_CREATE_CQ,
			.len = cpu_to_le16(IONIC_ADMIN_CREATE_CQ_IN_V1_LEN),
			.cmd.create_cq = {
				.eq_id = cpu_to_le32(cq->eqid),
				.depth_log2 = cq->q.depth_log2,
				.stride_log2 = cq->q.stride_log2,
				.page_size_log2 = buf->page_size_log2,
				.tbl_index = cpu_to_le32(~0),
				.map_count = cpu_to_le32(buf->tbl_pages),
				.dma_addr = ionic_pgtbl_dma(buf, 0),
				.dbid_flags = cpu_to_le16(dbid),
				.id_ver = cpu_to_le32(cq->cqid),
			}
		}
	};

	if (dev->lif_cfg.admin_opcodes <= IONIC_V1_ADMIN_CREATE_CQ)
		return -EBADRQC;

	ionic_admin_post(dev, &wr);

	return ionic_admin_wait(dev, &wr, 0);
}

static int ionic_destroy_cq_cmd(struct ionic_ibdev *dev, u32 cqid)
{
	struct ionic_admin_wr wr = {
		.work = COMPLETION_INITIALIZER_ONSTACK(wr.work),
		.wqe = {
			.op = IONIC_V1_ADMIN_DESTROY_CQ,
			.len = cpu_to_le16(IONIC_ADMIN_DESTROY_CQ_IN_V1_LEN),
			.cmd.destroy_cq = {
				.cq_id = cpu_to_le32(cqid),
			},
		}
	};

	if (dev->lif_cfg.admin_opcodes <= IONIC_V1_ADMIN_DESTROY_CQ)
		return -EBADRQC;

	ionic_admin_post(dev, &wr);

	return ionic_admin_wait(dev, &wr, IONIC_ADMIN_F_TEARDOWN);
}

int ionic_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		    struct uverbs_attr_bundle *attrs)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibcq->device);
	struct ib_udata *udata = &attrs->driver_udata;
	struct ionic_ctx *ctx =
		rdma_udata_to_drv_context(udata, struct ionic_ctx, ibctx);
	struct ionic_vcq *vcq = to_ionic_vcq(ibcq);
	struct ionic_tbl_buf buf = {};
	struct ionic_cq_resp resp;
	struct ionic_cq_req req;
	int udma_idx = 0, rc;

	if (udata) {
		rc = ib_copy_from_udata(&req, udata, sizeof(req));
		if (rc)
			return rc;
	}

	vcq->udma_mask = BIT(dev->lif_cfg.udma_count) - 1;

	if (udata)
		vcq->udma_mask &= req.udma_mask;

	if (!vcq->udma_mask) {
		rc = -EINVAL;
		goto err_init;
	}

	for (; udma_idx < dev->lif_cfg.udma_count; ++udma_idx) {
		if (!(vcq->udma_mask & BIT(udma_idx)))
			continue;

		rc = ionic_create_cq_common(vcq, &buf, attr, ctx, udata,
					    &req.cq[udma_idx],
					    &resp.cqid[udma_idx],
					    udma_idx);
		if (rc)
			goto err_init;

		rc = ionic_create_cq_cmd(dev, ctx, &vcq->cq[udma_idx], &buf);
		if (rc)
			goto err_cmd;

		ionic_pgtbl_unbuf(dev, &buf);
	}

	vcq->ibcq.cqe = attr->cqe;

	if (udata) {
		resp.udma_mask = vcq->udma_mask;

		rc = ib_copy_to_udata(udata, &resp, sizeof(resp));
		if (rc)
			goto err_resp;
	}

	return 0;

err_resp:
	while (udma_idx) {
		--udma_idx;
		if (!(vcq->udma_mask & BIT(udma_idx)))
			continue;
		ionic_destroy_cq_cmd(dev, vcq->cq[udma_idx].cqid);
err_cmd:
		ionic_pgtbl_unbuf(dev, &buf);
		ionic_destroy_cq_common(dev, &vcq->cq[udma_idx]);
err_init:
		;
	}

	return rc;
}

int ionic_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibcq->device);
	struct ionic_vcq *vcq = to_ionic_vcq(ibcq);
	int udma_idx, rc_tmp, rc = 0;

	for (udma_idx = dev->lif_cfg.udma_count; udma_idx; ) {
		--udma_idx;

		if (!(vcq->udma_mask & BIT(udma_idx)))
			continue;

		rc_tmp = ionic_destroy_cq_cmd(dev, vcq->cq[udma_idx].cqid);
		if (rc_tmp) {
			if (!rc)
				rc = rc_tmp;

			continue;
		}

		ionic_destroy_cq_common(dev, &vcq->cq[udma_idx]);
	}

	return rc;
}

static bool pd_remote_privileged(struct ib_pd *pd)
{
	return pd->flags & IB_PD_UNSAFE_GLOBAL_RKEY;
}

static int ionic_create_qp_cmd(struct ionic_ibdev *dev,
			       struct ionic_pd *pd,
			       struct ionic_cq *send_cq,
			       struct ionic_cq *recv_cq,
			       struct ionic_qp *qp,
			       struct ionic_tbl_buf *sq_buf,
			       struct ionic_tbl_buf *rq_buf,
			       struct ib_qp_init_attr *attr)
{
	const u16 dbid = ionic_obj_dbid(dev, pd->ibpd.uobject);
	const u32 flags = to_ionic_qp_flags(0, 0,
					    qp->sq_cmb & IONIC_CMB_ENABLE,
					    qp->rq_cmb & IONIC_CMB_ENABLE,
					    qp->sq_spec, qp->rq_spec,
					    pd->flags & IONIC_QPF_PRIVILEGED,
					    pd_remote_privileged(&pd->ibpd));
	struct ionic_admin_wr wr = {
		.work = COMPLETION_INITIALIZER_ONSTACK(wr.work),
		.wqe = {
			.op = IONIC_V1_ADMIN_CREATE_QP,
			.len = cpu_to_le16(IONIC_ADMIN_CREATE_QP_IN_V1_LEN),
			.cmd.create_qp = {
				.pd_id = cpu_to_le32(pd->pdid),
				.priv_flags = cpu_to_be32(flags),
				.type_state = to_ionic_qp_type(attr->qp_type),
				.dbid_flags = cpu_to_le16(dbid),
				.id_ver = cpu_to_le32(qp->qpid),
			}
		}
	};

	if (dev->lif_cfg.admin_opcodes <= IONIC_V1_ADMIN_CREATE_QP)
		return -EBADRQC;

	if (qp->has_sq) {
		wr.wqe.cmd.create_qp.sq_cq_id = cpu_to_le32(send_cq->cqid);
		wr.wqe.cmd.create_qp.sq_depth_log2 = qp->sq.depth_log2;
		wr.wqe.cmd.create_qp.sq_stride_log2 = qp->sq.stride_log2;
		wr.wqe.cmd.create_qp.sq_page_size_log2 = sq_buf->page_size_log2;
		wr.wqe.cmd.create_qp.sq_tbl_index_xrcd_id = cpu_to_le32(~0);
		wr.wqe.cmd.create_qp.sq_map_count =
			cpu_to_le32(sq_buf->tbl_pages);
		wr.wqe.cmd.create_qp.sq_dma_addr = ionic_pgtbl_dma(sq_buf, 0);
	}

	if (qp->has_rq) {
		wr.wqe.cmd.create_qp.rq_cq_id = cpu_to_le32(recv_cq->cqid);
		wr.wqe.cmd.create_qp.rq_depth_log2 = qp->rq.depth_log2;
		wr.wqe.cmd.create_qp.rq_stride_log2 = qp->rq.stride_log2;
		wr.wqe.cmd.create_qp.rq_page_size_log2 = rq_buf->page_size_log2;
		wr.wqe.cmd.create_qp.rq_tbl_index_srq_id = cpu_to_le32(~0);
		wr.wqe.cmd.create_qp.rq_map_count =
			cpu_to_le32(rq_buf->tbl_pages);
		wr.wqe.cmd.create_qp.rq_dma_addr = ionic_pgtbl_dma(rq_buf, 0);
	}

	ionic_admin_post(dev, &wr);

	return ionic_admin_wait(dev, &wr, 0);
}

static int ionic_modify_qp_cmd(struct ionic_ibdev *dev,
			       struct ionic_pd *pd,
			       struct ionic_qp *qp,
			       struct ib_qp_attr *attr,
			       int mask)
{
	const u32 flags = to_ionic_qp_flags(attr->qp_access_flags,
					    attr->en_sqd_async_notify,
					    qp->sq_cmb & IONIC_CMB_ENABLE,
					    qp->rq_cmb & IONIC_CMB_ENABLE,
					    qp->sq_spec, qp->rq_spec,
					    pd->flags & IONIC_QPF_PRIVILEGED,
					    pd_remote_privileged(qp->ibqp.pd));
	const u8 state = to_ionic_qp_modify_state(attr->qp_state,
						  attr->cur_qp_state);
	struct ionic_admin_wr wr = {
		.work = COMPLETION_INITIALIZER_ONSTACK(wr.work),
		.wqe = {
			.op = IONIC_V1_ADMIN_MODIFY_QP,
			.len = cpu_to_le16(IONIC_ADMIN_MODIFY_QP_IN_V1_LEN),
			.cmd.mod_qp = {
				.attr_mask = cpu_to_be32(mask),
				.access_flags = cpu_to_be16(flags),
				.rq_psn = cpu_to_le32(attr->rq_psn),
				.sq_psn = cpu_to_le32(attr->sq_psn),
				.rate_limit_kbps =
					cpu_to_le32(attr->rate_limit),
				.pmtu = (attr->path_mtu + 7),
				.retry = (attr->retry_cnt |
					  (attr->rnr_retry << 4)),
				.rnr_timer = attr->min_rnr_timer,
				.retry_timeout = attr->timeout,
				.type_state = state,
				.id_ver = cpu_to_le32(qp->qpid),
			}
		}
	};
	const struct ib_global_route *grh = rdma_ah_read_grh(&attr->ah_attr);
	void *hdr_buf = NULL;
	dma_addr_t hdr_dma = 0;
	int rc, hdr_len = 0;
	u16 sport;

	if (dev->lif_cfg.admin_opcodes <= IONIC_V1_ADMIN_MODIFY_QP)
		return -EBADRQC;

	if ((mask & IB_QP_MAX_DEST_RD_ATOMIC) && attr->max_dest_rd_atomic) {
		/* Note, round up/down was already done for allocating
		 * resources on the device. The allocation order is in cache
		 * line size.  We can't use the order of the resource
		 * allocation to determine the order wqes here, because for
		 * queue length <= one cache line it is not distinct.
		 *
		 * Therefore, order wqes is computed again here.
		 *
		 * Account for hole and round up to the next order.
		 */
		wr.wqe.cmd.mod_qp.rsq_depth =
			order_base_2(attr->max_dest_rd_atomic + 1);
		wr.wqe.cmd.mod_qp.rsq_index = cpu_to_le32(~0);
	}

	if ((mask & IB_QP_MAX_QP_RD_ATOMIC) && attr->max_rd_atomic) {
		/* Account for hole and round down to the next order */
		wr.wqe.cmd.mod_qp.rrq_depth =
			order_base_2(attr->max_rd_atomic + 2) - 1;
		wr.wqe.cmd.mod_qp.rrq_index = cpu_to_le32(~0);
	}

	if (qp->ibqp.qp_type == IB_QPT_RC || qp->ibqp.qp_type == IB_QPT_UC)
		wr.wqe.cmd.mod_qp.qkey_dest_qpn =
			cpu_to_le32(attr->dest_qp_num);
	else
		wr.wqe.cmd.mod_qp.qkey_dest_qpn = cpu_to_le32(attr->qkey);

	if (mask & IB_QP_AV) {
		if (!qp->hdr)
			return -ENOMEM;

		sport = rdma_get_udp_sport(grh->flow_label,
					   qp->qpid,
					   attr->dest_qp_num);

		rc = ionic_build_hdr(dev, qp->hdr, &attr->ah_attr, sport, true);
		if (rc)
			return rc;

		qp->sgid_index = grh->sgid_index;

		hdr_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!hdr_buf)
			return -ENOMEM;

		hdr_len = ib_ud_header_pack(qp->hdr, hdr_buf);
		hdr_len -= IB_BTH_BYTES;
		hdr_len -= IB_DETH_BYTES;
		ibdev_dbg(&dev->ibdev, "roce packet header template\n");
		print_hex_dump_debug("hdr ", DUMP_PREFIX_OFFSET, 16, 1,
				     hdr_buf, hdr_len, true);

		hdr_dma = dma_map_single(dev->lif_cfg.hwdev, hdr_buf, hdr_len,
					 DMA_TO_DEVICE);

		rc = dma_mapping_error(dev->lif_cfg.hwdev, hdr_dma);
		if (rc)
			goto err_dma;

		if (qp->hdr->ipv4_present) {
			wr.wqe.cmd.mod_qp.tfp_csum_profile =
				qp->hdr->vlan_present ?
					IONIC_TFP_CSUM_PROF_ETH_QTAG_IPV4_UDP :
					IONIC_TFP_CSUM_PROF_ETH_IPV4_UDP;
		} else {
			wr.wqe.cmd.mod_qp.tfp_csum_profile =
				qp->hdr->vlan_present ?
					IONIC_TFP_CSUM_PROF_ETH_QTAG_IPV6_UDP :
					IONIC_TFP_CSUM_PROF_ETH_IPV6_UDP;
		}

		wr.wqe.cmd.mod_qp.ah_id_len =
			cpu_to_le32(qp->ahid | (hdr_len << 24));
		wr.wqe.cmd.mod_qp.dma_addr = cpu_to_le64(hdr_dma);

		wr.wqe.cmd.mod_qp.en_pcp = attr->ah_attr.sl;
		wr.wqe.cmd.mod_qp.ip_dscp = grh->traffic_class >> 2;
	}

	ionic_admin_post(dev, &wr);

	rc = ionic_admin_wait(dev, &wr, 0);

	if (mask & IB_QP_AV)
		dma_unmap_single(dev->lif_cfg.hwdev, hdr_dma, hdr_len,
				 DMA_TO_DEVICE);
err_dma:
	if (mask & IB_QP_AV)
		kfree(hdr_buf);

	return rc;
}

static int ionic_query_qp_cmd(struct ionic_ibdev *dev,
			      struct ionic_qp *qp,
			      struct ib_qp_attr *attr,
			      int mask)
{
	struct ionic_admin_wr wr = {
		.work = COMPLETION_INITIALIZER_ONSTACK(wr.work),
		.wqe = {
			.op = IONIC_V1_ADMIN_QUERY_QP,
			.len = cpu_to_le16(IONIC_ADMIN_QUERY_QP_IN_V1_LEN),
			.cmd.query_qp = {
				.id_ver = cpu_to_le32(qp->qpid),
			},
		}
	};
	struct ionic_v1_admin_query_qp_sq *query_sqbuf;
	struct ionic_v1_admin_query_qp_rq *query_rqbuf;
	dma_addr_t query_sqdma;
	dma_addr_t query_rqdma;
	dma_addr_t hdr_dma = 0;
	void *hdr_buf = NULL;
	int flags, rc;

	if (dev->lif_cfg.admin_opcodes <= IONIC_V1_ADMIN_QUERY_QP)
		return -EBADRQC;

	if (qp->has_sq) {
		bool expdb = !!(qp->sq_cmb & IONIC_CMB_EXPDB);

		attr->cap.max_send_sge =
			ionic_v1_send_wqe_max_sge(qp->sq.stride_log2,
						  qp->sq_spec,
						  expdb);
		attr->cap.max_inline_data =
			ionic_v1_send_wqe_max_data(qp->sq.stride_log2, expdb);
	}

	if (qp->has_rq) {
		attr->cap.max_recv_sge =
			ionic_v1_recv_wqe_max_sge(qp->rq.stride_log2,
						  qp->rq_spec,
						  qp->rq_cmb & IONIC_CMB_EXPDB);
	}

	query_sqbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!query_sqbuf)
		return -ENOMEM;

	query_rqbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!query_rqbuf) {
		rc = -ENOMEM;
		goto err_rqbuf;
	}

	query_sqdma = dma_map_single(dev->lif_cfg.hwdev, query_sqbuf, PAGE_SIZE,
				     DMA_FROM_DEVICE);
	rc = dma_mapping_error(dev->lif_cfg.hwdev, query_sqdma);
	if (rc)
		goto err_sqdma;

	query_rqdma = dma_map_single(dev->lif_cfg.hwdev, query_rqbuf, PAGE_SIZE,
				     DMA_FROM_DEVICE);
	rc = dma_mapping_error(dev->lif_cfg.hwdev, query_rqdma);
	if (rc)
		goto err_rqdma;

	if (mask & IB_QP_AV) {
		hdr_buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!hdr_buf) {
			rc = -ENOMEM;
			goto err_hdrbuf;
		}

		hdr_dma = dma_map_single(dev->lif_cfg.hwdev, hdr_buf,
					 PAGE_SIZE, DMA_FROM_DEVICE);
		rc = dma_mapping_error(dev->lif_cfg.hwdev, hdr_dma);
		if (rc)
			goto err_hdrdma;
	}

	wr.wqe.cmd.query_qp.sq_dma_addr = cpu_to_le64(query_sqdma);
	wr.wqe.cmd.query_qp.rq_dma_addr = cpu_to_le64(query_rqdma);
	wr.wqe.cmd.query_qp.hdr_dma_addr = cpu_to_le64(hdr_dma);
	wr.wqe.cmd.query_qp.ah_id = cpu_to_le32(qp->ahid);

	ionic_admin_post(dev, &wr);

	rc = ionic_admin_wait(dev, &wr, 0);

	if (rc)
		goto err_hdrdma;

	flags = be16_to_cpu(query_sqbuf->access_perms_flags |
			    query_rqbuf->access_perms_flags);

	print_hex_dump_debug("sqbuf ", DUMP_PREFIX_OFFSET, 16, 1,
			     query_sqbuf, sizeof(*query_sqbuf), true);
	print_hex_dump_debug("rqbuf ", DUMP_PREFIX_OFFSET, 16, 1,
			     query_rqbuf, sizeof(*query_rqbuf), true);
	ibdev_dbg(&dev->ibdev, "query qp %u state_pmtu %#x flags %#x",
		  qp->qpid, query_rqbuf->state_pmtu, flags);

	attr->qp_state = from_ionic_qp_state(query_rqbuf->state_pmtu >> 4);
	attr->cur_qp_state = attr->qp_state;
	attr->path_mtu = (query_rqbuf->state_pmtu & 0xf) - 7;
	attr->path_mig_state = IB_MIG_MIGRATED;
	attr->qkey = be32_to_cpu(query_sqbuf->qkey_dest_qpn);
	attr->rq_psn = be32_to_cpu(query_sqbuf->rq_psn);
	attr->sq_psn = be32_to_cpu(query_rqbuf->sq_psn);
	attr->dest_qp_num = attr->qkey;
	attr->qp_access_flags = from_ionic_qp_flags(flags);
	attr->pkey_index = 0;
	attr->alt_pkey_index = 0;
	attr->en_sqd_async_notify = !!(flags & IONIC_QPF_SQD_NOTIFY);
	attr->sq_draining = !!(flags & IONIC_QPF_SQ_DRAINING);
	attr->max_rd_atomic = BIT(query_rqbuf->rrq_depth) - 1;
	attr->max_dest_rd_atomic = BIT(query_rqbuf->rsq_depth) - 1;
	attr->min_rnr_timer = query_sqbuf->rnr_timer;
	attr->port_num = 0;
	attr->timeout = query_sqbuf->retry_timeout;
	attr->retry_cnt = query_rqbuf->retry_rnrtry & 0xf;
	attr->rnr_retry = query_rqbuf->retry_rnrtry >> 4;
	attr->alt_port_num = 0;
	attr->alt_timeout = 0;
	attr->rate_limit = be32_to_cpu(query_sqbuf->rate_limit_kbps);

	if (mask & IB_QP_AV)
		ionic_set_ah_attr(dev, &attr->ah_attr,
				  qp->hdr, qp->sgid_index);

err_hdrdma:
	if (mask & IB_QP_AV) {
		dma_unmap_single(dev->lif_cfg.hwdev, hdr_dma,
				 PAGE_SIZE, DMA_FROM_DEVICE);
		kfree(hdr_buf);
	}
err_hdrbuf:
	dma_unmap_single(dev->lif_cfg.hwdev, query_rqdma, sizeof(*query_rqbuf),
			 DMA_FROM_DEVICE);
err_rqdma:
	dma_unmap_single(dev->lif_cfg.hwdev, query_sqdma, sizeof(*query_sqbuf),
			 DMA_FROM_DEVICE);
err_sqdma:
	kfree(query_rqbuf);
err_rqbuf:
	kfree(query_sqbuf);

	return rc;
}

static int ionic_destroy_qp_cmd(struct ionic_ibdev *dev, u32 qpid)
{
	struct ionic_admin_wr wr = {
		.work = COMPLETION_INITIALIZER_ONSTACK(wr.work),
		.wqe = {
			.op = IONIC_V1_ADMIN_DESTROY_QP,
			.len = cpu_to_le16(IONIC_ADMIN_DESTROY_QP_IN_V1_LEN),
			.cmd.destroy_qp = {
				.qp_id = cpu_to_le32(qpid),
			},
		}
	};

	if (dev->lif_cfg.admin_opcodes <= IONIC_V1_ADMIN_DESTROY_QP)
		return -EBADRQC;

	ionic_admin_post(dev, &wr);

	return ionic_admin_wait(dev, &wr, IONIC_ADMIN_F_TEARDOWN);
}

static bool ionic_expdb_wqe_size_supported(struct ionic_ibdev *dev,
					   uint32_t wqe_size)
{
	switch (wqe_size) {
	case 64: return dev->lif_cfg.expdb_mask & IONIC_EXPDB_64;
	case 128: return dev->lif_cfg.expdb_mask & IONIC_EXPDB_128;
	case 256: return dev->lif_cfg.expdb_mask & IONIC_EXPDB_256;
	case 512: return dev->lif_cfg.expdb_mask & IONIC_EXPDB_512;
	}

	return false;
}

static void ionic_qp_sq_init_cmb(struct ionic_ibdev *dev,
				 struct ionic_qp *qp,
				 struct ib_udata *udata,
				 int max_data)
{
	u8 expdb_stride_log2 = 0;
	bool expdb;
	int rc;

	if (!(qp->sq_cmb & IONIC_CMB_ENABLE))
		goto not_in_cmb;

	if (qp->sq_cmb & ~IONIC_CMB_SUPPORTED) {
		if (qp->sq_cmb & IONIC_CMB_REQUIRE)
			goto not_in_cmb;

		qp->sq_cmb &= IONIC_CMB_SUPPORTED;
	}

	if ((qp->sq_cmb & IONIC_CMB_EXPDB) && !dev->lif_cfg.sq_expdb) {
		if (qp->sq_cmb & IONIC_CMB_REQUIRE)
			goto not_in_cmb;

		qp->sq_cmb &= ~IONIC_CMB_EXPDB;
	}

	qp->sq_cmb_order = order_base_2(qp->sq.size / PAGE_SIZE);

	if (qp->sq_cmb_order >= IONIC_SQCMB_ORDER)
		goto not_in_cmb;

	if (qp->sq_cmb & IONIC_CMB_EXPDB)
		expdb_stride_log2 = qp->sq.stride_log2;

	rc = ionic_get_cmb(dev->lif_cfg.lif, &qp->sq_cmb_pgid,
			   &qp->sq_cmb_addr, qp->sq_cmb_order,
			   expdb_stride_log2, &expdb);
	if (rc)
		goto not_in_cmb;

	if ((qp->sq_cmb & IONIC_CMB_EXPDB) && !expdb) {
		if (qp->sq_cmb & IONIC_CMB_REQUIRE)
			goto err_map;

		qp->sq_cmb &= ~IONIC_CMB_EXPDB;
	}

	return;

err_map:
	ionic_put_cmb(dev->lif_cfg.lif, qp->sq_cmb_pgid, qp->sq_cmb_order);
not_in_cmb:
	if (qp->sq_cmb & IONIC_CMB_REQUIRE)
		ibdev_dbg(&dev->ibdev, "could not place sq in cmb as required\n");

	qp->sq_cmb = 0;
	qp->sq_cmb_order = IONIC_RES_INVALID;
	qp->sq_cmb_pgid = 0;
	qp->sq_cmb_addr = 0;
}

static void ionic_qp_sq_destroy_cmb(struct ionic_ibdev *dev,
				    struct ionic_ctx *ctx,
				    struct ionic_qp *qp)
{
	if (!(qp->sq_cmb & IONIC_CMB_ENABLE))
		return;

	if (ctx)
		rdma_user_mmap_entry_remove(qp->mmap_sq_cmb);

	ionic_put_cmb(dev->lif_cfg.lif, qp->sq_cmb_pgid, qp->sq_cmb_order);
}

static int ionic_qp_sq_init(struct ionic_ibdev *dev, struct ionic_ctx *ctx,
			    struct ionic_qp *qp, struct ionic_qdesc *sq,
			    struct ionic_tbl_buf *buf, int max_wr, int max_sge,
			    int max_data, int sq_spec, struct ib_udata *udata)
{
	u32 wqe_size;
	int rc = 0;

	qp->sq_msn_prod = 0;
	qp->sq_msn_cons = 0;

	if (!qp->has_sq) {
		if (buf) {
			buf->tbl_buf = NULL;
			buf->tbl_limit = 0;
			buf->tbl_pages = 0;
		}
		if (udata)
			rc = ionic_validate_qdesc_zero(sq);

		return rc;
	}

	rc = -EINVAL;

	if (max_wr < 0 || max_wr > 0xffff)
		return rc;

	if (max_sge < 1)
		return rc;

	if (max_sge > min(ionic_v1_send_wqe_max_sge(dev->lif_cfg.max_stride, 0,
						    qp->sq_cmb &
						    IONIC_CMB_EXPDB),
			  IONIC_SPEC_HIGH))
		return rc;

	if (max_data < 0)
		return rc;

	if (max_data > ionic_v1_send_wqe_max_data(dev->lif_cfg.max_stride,
						  qp->sq_cmb & IONIC_CMB_EXPDB))
		return rc;

	if (udata) {
		rc = ionic_validate_qdesc(sq);
		if (rc)
			return rc;

		qp->sq_spec = sq_spec;

		qp->sq.ptr = NULL;
		qp->sq.size = sq->size;
		qp->sq.mask = sq->mask;
		qp->sq.depth_log2 = sq->depth_log2;
		qp->sq.stride_log2 = sq->stride_log2;

		qp->sq_meta = NULL;
		qp->sq_msn_idx = NULL;

		qp->sq_umem = ib_umem_get(&dev->ibdev, sq->addr, sq->size, 0);
		if (IS_ERR(qp->sq_umem))
			return PTR_ERR(qp->sq_umem);
	} else {
		qp->sq_umem = NULL;

		qp->sq_spec = ionic_v1_use_spec_sge(max_sge, sq_spec);
		if (sq_spec && !qp->sq_spec)
			ibdev_dbg(&dev->ibdev,
				  "init sq: max_sge %u disables spec\n",
				  max_sge);

		if (qp->sq_cmb & IONIC_CMB_EXPDB) {
			wqe_size = ionic_v1_send_wqe_min_size(max_sge, max_data,
							      qp->sq_spec,
							      true);

			if (!ionic_expdb_wqe_size_supported(dev, wqe_size))
				qp->sq_cmb &= ~IONIC_CMB_EXPDB;
		}

		if (!(qp->sq_cmb & IONIC_CMB_EXPDB))
			wqe_size = ionic_v1_send_wqe_min_size(max_sge, max_data,
							      qp->sq_spec,
							      false);

		rc = ionic_queue_init(&qp->sq, dev->lif_cfg.hwdev,
				      max_wr, wqe_size);
		if (rc)
			return rc;

		ionic_queue_dbell_init(&qp->sq, qp->qpid);

		qp->sq_meta = kmalloc_array((u32)qp->sq.mask + 1,
					    sizeof(*qp->sq_meta),
					    GFP_KERNEL);
		if (!qp->sq_meta) {
			rc = -ENOMEM;
			goto err_sq_meta;
		}

		qp->sq_msn_idx = kmalloc_array((u32)qp->sq.mask + 1,
					       sizeof(*qp->sq_msn_idx),
					       GFP_KERNEL);
		if (!qp->sq_msn_idx) {
			rc = -ENOMEM;
			goto err_sq_msn;
		}
	}

	ionic_qp_sq_init_cmb(dev, qp, udata, max_data);

	if (qp->sq_cmb & IONIC_CMB_ENABLE)
		rc = ionic_pgtbl_init(dev, buf, NULL,
				      (u64)qp->sq_cmb_pgid << PAGE_SHIFT,
				      1, PAGE_SIZE);
	else
		rc = ionic_pgtbl_init(dev, buf,
				      qp->sq_umem, qp->sq.dma, 1, PAGE_SIZE);
	if (rc)
		goto err_sq_tbl;

	return 0;

err_sq_tbl:
	ionic_qp_sq_destroy_cmb(dev, ctx, qp);
	kfree(qp->sq_msn_idx);
err_sq_msn:
	kfree(qp->sq_meta);
err_sq_meta:
	if (qp->sq_umem)
		ib_umem_release(qp->sq_umem);
	else
		ionic_queue_destroy(&qp->sq, dev->lif_cfg.hwdev);
	return rc;
}

static void ionic_qp_sq_destroy(struct ionic_ibdev *dev,
				struct ionic_ctx *ctx,
				struct ionic_qp *qp)
{
	if (!qp->has_sq)
		return;

	ionic_qp_sq_destroy_cmb(dev, ctx, qp);

	kfree(qp->sq_msn_idx);
	kfree(qp->sq_meta);

	if (qp->sq_umem)
		ib_umem_release(qp->sq_umem);
	else
		ionic_queue_destroy(&qp->sq, dev->lif_cfg.hwdev);
}

static void ionic_qp_rq_init_cmb(struct ionic_ibdev *dev,
				 struct ionic_qp *qp,
				 struct ib_udata *udata)
{
	u8 expdb_stride_log2 = 0;
	bool expdb;
	int rc;

	if (!(qp->rq_cmb & IONIC_CMB_ENABLE))
		goto not_in_cmb;

	if (qp->rq_cmb & ~IONIC_CMB_SUPPORTED) {
		if (qp->rq_cmb & IONIC_CMB_REQUIRE)
			goto not_in_cmb;

		qp->rq_cmb &= IONIC_CMB_SUPPORTED;
	}

	if ((qp->rq_cmb & IONIC_CMB_EXPDB) && !dev->lif_cfg.rq_expdb) {
		if (qp->rq_cmb & IONIC_CMB_REQUIRE)
			goto not_in_cmb;

		qp->rq_cmb &= ~IONIC_CMB_EXPDB;
	}

	qp->rq_cmb_order = order_base_2(qp->rq.size / PAGE_SIZE);

	if (qp->rq_cmb_order >= IONIC_RQCMB_ORDER)
		goto not_in_cmb;

	if (qp->rq_cmb & IONIC_CMB_EXPDB)
		expdb_stride_log2 = qp->rq.stride_log2;

	rc = ionic_get_cmb(dev->lif_cfg.lif, &qp->rq_cmb_pgid,
			   &qp->rq_cmb_addr, qp->rq_cmb_order,
			   expdb_stride_log2, &expdb);
	if (rc)
		goto not_in_cmb;

	if ((qp->rq_cmb & IONIC_CMB_EXPDB) && !expdb) {
		if (qp->rq_cmb & IONIC_CMB_REQUIRE)
			goto err_map;

		qp->rq_cmb &= ~IONIC_CMB_EXPDB;
	}

	return;

err_map:
	ionic_put_cmb(dev->lif_cfg.lif, qp->rq_cmb_pgid, qp->rq_cmb_order);
not_in_cmb:
	if (qp->rq_cmb & IONIC_CMB_REQUIRE)
		ibdev_dbg(&dev->ibdev, "could not place rq in cmb as required\n");

	qp->rq_cmb = 0;
	qp->rq_cmb_order = IONIC_RES_INVALID;
	qp->rq_cmb_pgid = 0;
	qp->rq_cmb_addr = 0;
}

static void ionic_qp_rq_destroy_cmb(struct ionic_ibdev *dev,
				    struct ionic_ctx *ctx,
				    struct ionic_qp *qp)
{
	if (!(qp->rq_cmb & IONIC_CMB_ENABLE))
		return;

	if (ctx)
		rdma_user_mmap_entry_remove(qp->mmap_rq_cmb);

	ionic_put_cmb(dev->lif_cfg.lif, qp->rq_cmb_pgid, qp->rq_cmb_order);
}

static int ionic_qp_rq_init(struct ionic_ibdev *dev, struct ionic_ctx *ctx,
			    struct ionic_qp *qp, struct ionic_qdesc *rq,
			    struct ionic_tbl_buf *buf, int max_wr, int max_sge,
			    int rq_spec, struct ib_udata *udata)
{
	int rc = 0, i;
	u32 wqe_size;

	if (!qp->has_rq) {
		if (buf) {
			buf->tbl_buf = NULL;
			buf->tbl_limit = 0;
			buf->tbl_pages = 0;
		}
		if (udata)
			rc = ionic_validate_qdesc_zero(rq);

		return rc;
	}

	rc = -EINVAL;

	if (max_wr < 0 || max_wr > 0xffff)
		return rc;

	if (max_sge < 1)
		return rc;

	if (max_sge > min(ionic_v1_recv_wqe_max_sge(dev->lif_cfg.max_stride, 0, false),
			  IONIC_SPEC_HIGH))
		return rc;

	if (udata) {
		rc = ionic_validate_qdesc(rq);
		if (rc)
			return rc;

		qp->rq_spec = rq_spec;

		qp->rq.ptr = NULL;
		qp->rq.size = rq->size;
		qp->rq.mask = rq->mask;
		qp->rq.depth_log2 = rq->depth_log2;
		qp->rq.stride_log2 = rq->stride_log2;

		qp->rq_meta = NULL;

		qp->rq_umem = ib_umem_get(&dev->ibdev, rq->addr, rq->size, 0);
		if (IS_ERR(qp->rq_umem))
			return PTR_ERR(qp->rq_umem);
	} else {
		qp->rq_umem = NULL;

		qp->rq_spec = ionic_v1_use_spec_sge(max_sge, rq_spec);
		if (rq_spec && !qp->rq_spec)
			ibdev_dbg(&dev->ibdev,
				  "init rq: max_sge %u disables spec\n",
				  max_sge);

		if (qp->rq_cmb & IONIC_CMB_EXPDB) {
			wqe_size = ionic_v1_recv_wqe_min_size(max_sge,
							      qp->rq_spec,
							      true);

			if (!ionic_expdb_wqe_size_supported(dev, wqe_size))
				qp->rq_cmb &= ~IONIC_CMB_EXPDB;
		}

		if (!(qp->rq_cmb & IONIC_CMB_EXPDB))
			wqe_size = ionic_v1_recv_wqe_min_size(max_sge,
							      qp->rq_spec,
							      false);

		rc = ionic_queue_init(&qp->rq, dev->lif_cfg.hwdev,
				      max_wr, wqe_size);
		if (rc)
			return rc;

		ionic_queue_dbell_init(&qp->rq, qp->qpid);

		qp->rq_meta = kmalloc_array((u32)qp->rq.mask + 1,
					    sizeof(*qp->rq_meta),
					    GFP_KERNEL);
		if (!qp->rq_meta) {
			rc = -ENOMEM;
			goto err_rq_meta;
		}

		for (i = 0; i < qp->rq.mask; ++i)
			qp->rq_meta[i].next = &qp->rq_meta[i + 1];
		qp->rq_meta[i].next = IONIC_META_LAST;
		qp->rq_meta_head = &qp->rq_meta[0];
	}

	ionic_qp_rq_init_cmb(dev, qp, udata);

	if (qp->rq_cmb & IONIC_CMB_ENABLE)
		rc = ionic_pgtbl_init(dev, buf, NULL,
				      (u64)qp->rq_cmb_pgid << PAGE_SHIFT,
				      1, PAGE_SIZE);
	else
		rc = ionic_pgtbl_init(dev, buf,
				      qp->rq_umem, qp->rq.dma, 1, PAGE_SIZE);
	if (rc)
		goto err_rq_tbl;

	return 0;

err_rq_tbl:
	ionic_qp_rq_destroy_cmb(dev, ctx, qp);
	kfree(qp->rq_meta);
err_rq_meta:
	if (qp->rq_umem)
		ib_umem_release(qp->rq_umem);
	else
		ionic_queue_destroy(&qp->rq, dev->lif_cfg.hwdev);
	return rc;
}

static void ionic_qp_rq_destroy(struct ionic_ibdev *dev,
				struct ionic_ctx *ctx,
				struct ionic_qp *qp)
{
	if (!qp->has_rq)
		return;

	ionic_qp_rq_destroy_cmb(dev, ctx, qp);

	kfree(qp->rq_meta);

	if (qp->rq_umem)
		ib_umem_release(qp->rq_umem);
	else
		ionic_queue_destroy(&qp->rq, dev->lif_cfg.hwdev);
}

int ionic_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *attr,
		    struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibqp->device);
	struct ionic_tbl_buf sq_buf = {}, rq_buf = {};
	struct ionic_pd *pd = to_ionic_pd(ibqp->pd);
	struct ionic_qp *qp = to_ionic_qp(ibqp);
	struct ionic_ctx *ctx =
		rdma_udata_to_drv_context(udata, struct ionic_ctx, ibctx);
	struct ionic_qp_resp resp = {};
	struct ionic_qp_req req = {};
	struct ionic_cq *cq;
	u8 udma_mask;
	void *entry;
	int rc;

	if (udata) {
		rc = ib_copy_from_udata(&req, udata, sizeof(req));
		if (rc)
			return rc;
	} else {
		req.sq_spec = IONIC_SPEC_HIGH;
		req.rq_spec = IONIC_SPEC_HIGH;
	}

	if (attr->qp_type == IB_QPT_SMI || attr->qp_type > IB_QPT_UD)
		return -EOPNOTSUPP;

	qp->state = IB_QPS_RESET;

	INIT_LIST_HEAD(&qp->cq_poll_sq);
	INIT_LIST_HEAD(&qp->cq_flush_sq);
	INIT_LIST_HEAD(&qp->cq_flush_rq);

	spin_lock_init(&qp->sq_lock);
	spin_lock_init(&qp->rq_lock);

	qp->has_sq = 1;
	qp->has_rq = 1;

	if (attr->qp_type == IB_QPT_GSI) {
		rc = ionic_get_gsi_qpid(dev, &qp->qpid);
	} else {
		udma_mask = BIT(dev->lif_cfg.udma_count) - 1;

		if (qp->has_sq)
			udma_mask &= to_ionic_vcq(attr->send_cq)->udma_mask;

		if (qp->has_rq)
			udma_mask &= to_ionic_vcq(attr->recv_cq)->udma_mask;

		if (udata && req.udma_mask)
			udma_mask &= req.udma_mask;

		if (!udma_mask)
			return -EINVAL;

		rc = ionic_get_qpid(dev, &qp->qpid, &qp->udma_idx, udma_mask);
	}
	if (rc)
		return rc;

	qp->sig_all = attr->sq_sig_type == IB_SIGNAL_ALL_WR;
	qp->has_ah = attr->qp_type == IB_QPT_RC;

	if (qp->has_ah) {
		qp->hdr = kzalloc(sizeof(*qp->hdr), GFP_KERNEL);
		if (!qp->hdr) {
			rc = -ENOMEM;
			goto err_ah_alloc;
		}

		rc = ionic_get_ahid(dev, &qp->ahid);
		if (rc)
			goto err_ahid;
	}

	if (udata) {
		if (req.rq_cmb & IONIC_CMB_ENABLE)
			qp->rq_cmb = req.rq_cmb;

		if (req.sq_cmb & IONIC_CMB_ENABLE)
			qp->sq_cmb = req.sq_cmb;
	}

	rc = ionic_qp_sq_init(dev, ctx, qp, &req.sq, &sq_buf,
			      attr->cap.max_send_wr, attr->cap.max_send_sge,
			      attr->cap.max_inline_data, req.sq_spec, udata);
	if (rc)
		goto err_sq;

	rc = ionic_qp_rq_init(dev, ctx, qp, &req.rq, &rq_buf,
			      attr->cap.max_recv_wr, attr->cap.max_recv_sge,
			      req.rq_spec, udata);
	if (rc)
		goto err_rq;

	rc = ionic_create_qp_cmd(dev, pd,
				 to_ionic_vcq_cq(attr->send_cq, qp->udma_idx),
				 to_ionic_vcq_cq(attr->recv_cq, qp->udma_idx),
				 qp, &sq_buf, &rq_buf, attr);
	if (rc)
		goto err_cmd;

	if (udata) {
		resp.qpid = qp->qpid;
		resp.udma_idx = qp->udma_idx;

		if (qp->sq_cmb & IONIC_CMB_ENABLE) {
			bool wc;

			if ((qp->sq_cmb & (IONIC_CMB_WC | IONIC_CMB_UC)) ==
				(IONIC_CMB_WC | IONIC_CMB_UC)) {
				ibdev_dbg(&dev->ibdev,
					  "Both sq_cmb flags IONIC_CMB_WC and IONIC_CMB_UC are set, using default driver mapping\n");
				qp->sq_cmb &= ~(IONIC_CMB_WC | IONIC_CMB_UC);
			}

			wc = (qp->sq_cmb & (IONIC_CMB_WC | IONIC_CMB_UC))
					!= IONIC_CMB_UC;

			/* let userspace know the mapping */
			if (wc)
				qp->sq_cmb |= IONIC_CMB_WC;
			else
				qp->sq_cmb |= IONIC_CMB_UC;

			qp->mmap_sq_cmb =
			    ionic_mmap_entry_insert(ctx,
						    qp->sq.size,
						    PHYS_PFN(qp->sq_cmb_addr),
						    wc ? IONIC_MMAP_WC : 0,
						    &resp.sq_cmb_offset);
			if (!qp->mmap_sq_cmb) {
				rc = -ENOMEM;
				goto err_mmap_sq;
			}

			resp.sq_cmb = qp->sq_cmb;
		}

		if (qp->rq_cmb & IONIC_CMB_ENABLE) {
			bool wc;

			if ((qp->rq_cmb & (IONIC_CMB_WC | IONIC_CMB_UC)) ==
				(IONIC_CMB_WC | IONIC_CMB_UC)) {
				ibdev_dbg(&dev->ibdev,
					  "Both rq_cmb flags IONIC_CMB_WC and IONIC_CMB_UC are set, using default driver mapping\n");
				qp->rq_cmb &= ~(IONIC_CMB_WC | IONIC_CMB_UC);
			}

			if (qp->rq_cmb & IONIC_CMB_EXPDB)
				wc = (qp->rq_cmb & (IONIC_CMB_WC | IONIC_CMB_UC))
					== IONIC_CMB_WC;
			else
				wc = (qp->rq_cmb & (IONIC_CMB_WC | IONIC_CMB_UC))
					!= IONIC_CMB_UC;

			/* let userspace know the mapping */
			if (wc)
				qp->rq_cmb |= IONIC_CMB_WC;
			else
				qp->rq_cmb |= IONIC_CMB_UC;

			qp->mmap_rq_cmb =
			    ionic_mmap_entry_insert(ctx,
						    qp->rq.size,
						    PHYS_PFN(qp->rq_cmb_addr),
						    wc ? IONIC_MMAP_WC : 0,
						    &resp.rq_cmb_offset);
			if (!qp->mmap_rq_cmb) {
				rc = -ENOMEM;
				goto err_mmap_rq;
			}

			resp.rq_cmb = qp->rq_cmb;
		}

		rc = ib_copy_to_udata(udata, &resp, sizeof(resp));
		if (rc)
			goto err_resp;
	}

	ionic_pgtbl_unbuf(dev, &rq_buf);
	ionic_pgtbl_unbuf(dev, &sq_buf);

	qp->ibqp.qp_num = qp->qpid;

	init_completion(&qp->qp_rel_comp);
	kref_init(&qp->qp_kref);

	entry = xa_store_irq(&dev->qp_tbl, qp->qpid, qp, GFP_KERNEL);
	if (entry) {
		if (!xa_is_err(entry))
			rc = -EINVAL;
		else
			rc = xa_err(entry);

		goto err_resp;
	}

	if (qp->has_sq) {
		cq = to_ionic_vcq_cq(attr->send_cq, qp->udma_idx);

		attr->cap.max_send_wr = qp->sq.mask;
		attr->cap.max_send_sge =
			ionic_v1_send_wqe_max_sge(qp->sq.stride_log2,
						  qp->sq_spec,
						  qp->sq_cmb & IONIC_CMB_EXPDB);
		attr->cap.max_inline_data =
			ionic_v1_send_wqe_max_data(qp->sq.stride_log2,
						   qp->sq_cmb &
						   IONIC_CMB_EXPDB);
		qp->sq_cqid = cq->cqid;
	}

	if (qp->has_rq) {
		cq = to_ionic_vcq_cq(attr->recv_cq, qp->udma_idx);

		attr->cap.max_recv_wr = qp->rq.mask;
		attr->cap.max_recv_sge =
			ionic_v1_recv_wqe_max_sge(qp->rq.stride_log2,
						  qp->rq_spec,
						  qp->rq_cmb & IONIC_CMB_EXPDB);
		qp->rq_cqid = cq->cqid;
	}

	return 0;

err_resp:
	if (udata && (qp->rq_cmb & IONIC_CMB_ENABLE))
		rdma_user_mmap_entry_remove(qp->mmap_rq_cmb);
err_mmap_rq:
	if (udata && (qp->sq_cmb & IONIC_CMB_ENABLE))
		rdma_user_mmap_entry_remove(qp->mmap_sq_cmb);
err_mmap_sq:
	ionic_destroy_qp_cmd(dev, qp->qpid);
err_cmd:
	ionic_pgtbl_unbuf(dev, &rq_buf);
	ionic_qp_rq_destroy(dev, ctx, qp);
err_rq:
	ionic_pgtbl_unbuf(dev, &sq_buf);
	ionic_qp_sq_destroy(dev, ctx, qp);
err_sq:
	if (qp->has_ah)
		ionic_put_ahid(dev, qp->ahid);
err_ahid:
	kfree(qp->hdr);
err_ah_alloc:
	ionic_put_qpid(dev, qp->qpid);
	return rc;
}

void ionic_notify_flush_cq(struct ionic_cq *cq)
{
	if (cq->flush && cq->vcq->ibcq.comp_handler)
		cq->vcq->ibcq.comp_handler(&cq->vcq->ibcq,
					   cq->vcq->ibcq.cq_context);
}

static void ionic_notify_qp_cqs(struct ionic_ibdev *dev, struct ionic_qp *qp)
{
	if (qp->ibqp.send_cq)
		ionic_notify_flush_cq(to_ionic_vcq_cq(qp->ibqp.send_cq,
						      qp->udma_idx));
	if (qp->ibqp.recv_cq && qp->ibqp.recv_cq != qp->ibqp.send_cq)
		ionic_notify_flush_cq(to_ionic_vcq_cq(qp->ibqp.recv_cq,
						      qp->udma_idx));
}

void ionic_flush_qp(struct ionic_ibdev *dev, struct ionic_qp *qp)
{
	unsigned long irqflags;
	struct ionic_cq *cq;

	if (qp->ibqp.send_cq) {
		cq = to_ionic_vcq_cq(qp->ibqp.send_cq, qp->udma_idx);

		/* Hold the CQ lock and QP sq_lock to set up flush */
		spin_lock_irqsave(&cq->lock, irqflags);
		spin_lock(&qp->sq_lock);
		qp->sq_flush = true;
		if (!ionic_queue_empty(&qp->sq)) {
			cq->flush = true;
			list_move_tail(&qp->cq_flush_sq, &cq->flush_sq);
		}
		spin_unlock(&qp->sq_lock);
		spin_unlock_irqrestore(&cq->lock, irqflags);
	}

	if (qp->ibqp.recv_cq) {
		cq = to_ionic_vcq_cq(qp->ibqp.recv_cq, qp->udma_idx);

		/* Hold the CQ lock and QP rq_lock to set up flush */
		spin_lock_irqsave(&cq->lock, irqflags);
		spin_lock(&qp->rq_lock);
		qp->rq_flush = true;
		if (!ionic_queue_empty(&qp->rq)) {
			cq->flush = true;
			list_move_tail(&qp->cq_flush_rq, &cq->flush_rq);
		}
		spin_unlock(&qp->rq_lock);
		spin_unlock_irqrestore(&cq->lock, irqflags);
	}
}

static void ionic_clean_cq(struct ionic_cq *cq, u32 qpid)
{
	struct ionic_v1_cqe *qcqe;
	int prod, qtf, qid, type;
	bool color;

	if (!cq->q.ptr)
		return;

	color = cq->color;
	prod = cq->q.prod;
	qcqe = ionic_queue_at(&cq->q, prod);

	while (color == ionic_v1_cqe_color(qcqe)) {
		qtf = ionic_v1_cqe_qtf(qcqe);
		qid = ionic_v1_cqe_qtf_qid(qtf);
		type = ionic_v1_cqe_qtf_type(qtf);

		if (qid == qpid && type != IONIC_V1_CQE_TYPE_ADMIN)
			ionic_v1_cqe_clean(qcqe);

		prod = ionic_queue_next(&cq->q, prod);
		qcqe = ionic_queue_at(&cq->q, prod);
		color = ionic_color_wrap(prod, color);
	}
}

static void ionic_reset_qp(struct ionic_ibdev *dev, struct ionic_qp *qp)
{
	unsigned long irqflags;
	struct ionic_cq *cq;
	int i;

	local_irq_save(irqflags);

	if (qp->ibqp.send_cq) {
		cq = to_ionic_vcq_cq(qp->ibqp.send_cq, qp->udma_idx);
		spin_lock(&cq->lock);
		ionic_clean_cq(cq, qp->qpid);
		spin_unlock(&cq->lock);
	}

	if (qp->ibqp.recv_cq) {
		cq = to_ionic_vcq_cq(qp->ibqp.recv_cq, qp->udma_idx);
		spin_lock(&cq->lock);
		ionic_clean_cq(cq, qp->qpid);
		spin_unlock(&cq->lock);
	}

	if (qp->has_sq) {
		spin_lock(&qp->sq_lock);
		qp->sq_flush = false;
		qp->sq_flush_rcvd = false;
		qp->sq_msn_prod = 0;
		qp->sq_msn_cons = 0;
		qp->sq.prod = 0;
		qp->sq.cons = 0;
		spin_unlock(&qp->sq_lock);
	}

	if (qp->has_rq) {
		spin_lock(&qp->rq_lock);
		qp->rq_flush = false;
		qp->rq.prod = 0;
		qp->rq.cons = 0;
		if (qp->rq_meta) {
			for (i = 0; i < qp->rq.mask; ++i)
				qp->rq_meta[i].next = &qp->rq_meta[i + 1];
			qp->rq_meta[i].next = IONIC_META_LAST;
		}
		qp->rq_meta_head = &qp->rq_meta[0];
		spin_unlock(&qp->rq_lock);
	}

	local_irq_restore(irqflags);
}

static bool ionic_qp_cur_state_is_ok(enum ib_qp_state q_state,
				     enum ib_qp_state attr_state)
{
	if (q_state == attr_state)
		return true;

	if (attr_state == IB_QPS_ERR)
		return true;

	if (attr_state == IB_QPS_SQE)
		return q_state == IB_QPS_RTS || q_state == IB_QPS_SQD;

	return false;
}

static int ionic_check_modify_qp(struct ionic_qp *qp, struct ib_qp_attr *attr,
				 int mask)
{
	enum ib_qp_state cur_state = (mask & IB_QP_CUR_STATE) ?
		attr->cur_qp_state : qp->state;
	enum ib_qp_state next_state = (mask & IB_QP_STATE) ?
		attr->qp_state : cur_state;

	if ((mask & IB_QP_CUR_STATE) &&
	    !ionic_qp_cur_state_is_ok(qp->state, attr->cur_qp_state))
		return -EINVAL;

	if (!ib_modify_qp_is_ok(cur_state, next_state, qp->ibqp.qp_type, mask))
		return -EINVAL;

	/* unprivileged qp not allowed privileged qkey */
	if ((mask & IB_QP_QKEY) && (attr->qkey & 0x80000000) &&
	    qp->ibqp.uobject)
		return -EPERM;

	return 0;
}

int ionic_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int mask,
		    struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibqp->device);
	struct ionic_pd *pd = to_ionic_pd(ibqp->pd);
	struct ionic_qp *qp = to_ionic_qp(ibqp);
	int rc;

	rc = ionic_check_modify_qp(qp, attr, mask);
	if (rc)
		return rc;

	if (mask & IB_QP_CAP)
		return -EINVAL;

	rc = ionic_modify_qp_cmd(dev, pd, qp, attr, mask);
	if (rc)
		return rc;

	if (mask & IB_QP_STATE) {
		qp->state = attr->qp_state;

		if (attr->qp_state == IB_QPS_ERR) {
			ionic_flush_qp(dev, qp);
			ionic_notify_qp_cqs(dev, qp);
		} else if (attr->qp_state == IB_QPS_RESET) {
			ionic_reset_qp(dev, qp);
		}
	}

	return 0;
}

int ionic_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		   int mask, struct ib_qp_init_attr *init_attr)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibqp->device);
	struct ionic_qp *qp = to_ionic_qp(ibqp);
	int rc;

	memset(attr, 0, sizeof(*attr));
	memset(init_attr, 0, sizeof(*init_attr));

	rc = ionic_query_qp_cmd(dev, qp, attr, mask);
	if (rc)
		return rc;

	if (qp->has_sq)
		attr->cap.max_send_wr = qp->sq.mask;

	if (qp->has_rq)
		attr->cap.max_recv_wr = qp->rq.mask;

	init_attr->event_handler = ibqp->event_handler;
	init_attr->qp_context = ibqp->qp_context;
	init_attr->send_cq = ibqp->send_cq;
	init_attr->recv_cq = ibqp->recv_cq;
	init_attr->srq = ibqp->srq;
	init_attr->xrcd = ibqp->xrcd;
	init_attr->cap = attr->cap;
	init_attr->sq_sig_type = qp->sig_all ?
		IB_SIGNAL_ALL_WR : IB_SIGNAL_REQ_WR;
	init_attr->qp_type = ibqp->qp_type;
	init_attr->create_flags = 0;
	init_attr->port_num = 0;
	init_attr->rwq_ind_tbl = ibqp->rwq_ind_tbl;
	init_attr->source_qpn = 0;

	return rc;
}

int ionic_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct ionic_ctx *ctx =
		rdma_udata_to_drv_context(udata, struct ionic_ctx, ibctx);
	struct ionic_ibdev *dev = to_ionic_ibdev(ibqp->device);
	struct ionic_qp *qp = to_ionic_qp(ibqp);
	unsigned long irqflags;
	struct ionic_cq *cq;
	int rc;

	rc = ionic_destroy_qp_cmd(dev, qp->qpid);
	if (rc)
		return rc;

	xa_erase_irq(&dev->qp_tbl, qp->qpid);

	kref_put(&qp->qp_kref, ionic_qp_complete);
	wait_for_completion(&qp->qp_rel_comp);

	if (qp->ibqp.send_cq) {
		cq = to_ionic_vcq_cq(qp->ibqp.send_cq, qp->udma_idx);
		spin_lock_irqsave(&cq->lock, irqflags);
		ionic_clean_cq(cq, qp->qpid);
		list_del(&qp->cq_poll_sq);
		list_del(&qp->cq_flush_sq);
		spin_unlock_irqrestore(&cq->lock, irqflags);
	}

	if (qp->ibqp.recv_cq) {
		cq = to_ionic_vcq_cq(qp->ibqp.recv_cq, qp->udma_idx);
		spin_lock_irqsave(&cq->lock, irqflags);
		ionic_clean_cq(cq, qp->qpid);
		list_del(&qp->cq_flush_rq);
		spin_unlock_irqrestore(&cq->lock, irqflags);
	}

	ionic_qp_rq_destroy(dev, ctx, qp);
	ionic_qp_sq_destroy(dev, ctx, qp);
	if (qp->has_ah) {
		ionic_put_ahid(dev, qp->ahid);
		kfree(qp->hdr);
	}
	ionic_put_qpid(dev, qp->qpid);

	return 0;
}
