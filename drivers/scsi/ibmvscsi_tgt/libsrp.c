// SPDX-License-Identifier: GPL-2.0-or-later
/*******************************************************************************
 * SCSI RDMA Protocol lib functions
 *
 * Copyright (C) 2006 FUJITA Tomonori <tomof@acm.org>
 * Copyright (C) 2016 Bryant G. Ly <bryantly@linux.vnet.ibm.com> IBM Corp.
 *
 ***********************************************************************/

#define pr_fmt(fmt)	"libsrp: " fmt

#include <linux/printk.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <scsi/srp.h>
#include <target/target_core_base.h>
#include "libsrp.h"
#include "ibmvscsi_tgt.h"

static int srp_iu_pool_alloc(struct srp_queue *q, size_t max,
			     struct srp_buf **ring)
{
	struct iu_entry *iue;
	int i;

	q->pool = kcalloc(max, sizeof(struct iu_entry *), GFP_KERNEL);
	if (!q->pool)
		return -ENOMEM;
	q->items = kcalloc(max, sizeof(struct iu_entry), GFP_KERNEL);
	if (!q->items)
		goto free_pool;

	spin_lock_init(&q->lock);
	kfifo_init(&q->queue, (void *)q->pool, max * sizeof(void *));

	for (i = 0, iue = q->items; i < max; i++) {
		kfifo_in(&q->queue, (void *)&iue, sizeof(void *));
		iue->sbuf = ring[i];
		iue++;
	}
	return 0;

free_pool:
	kfree(q->pool);
	return -ENOMEM;
}

static void srp_iu_pool_free(struct srp_queue *q)
{
	kfree(q->items);
	kfree(q->pool);
}

static struct srp_buf **srp_ring_alloc(struct device *dev,
				       size_t max, size_t size)
{
	struct srp_buf **ring;
	int i;

	ring = kcalloc(max, sizeof(struct srp_buf *), GFP_KERNEL);
	if (!ring)
		return NULL;

	for (i = 0; i < max; i++) {
		ring[i] = kzalloc(sizeof(*ring[i]), GFP_KERNEL);
		if (!ring[i])
			goto out;
		ring[i]->buf = dma_alloc_coherent(dev, size, &ring[i]->dma,
						  GFP_KERNEL);
		if (!ring[i]->buf)
			goto out;
	}
	return ring;

out:
	for (i = 0; i < max && ring[i]; i++) {
		if (ring[i]->buf) {
			dma_free_coherent(dev, size, ring[i]->buf,
					  ring[i]->dma);
		}
		kfree(ring[i]);
	}
	kfree(ring);

	return NULL;
}

static void srp_ring_free(struct device *dev, struct srp_buf **ring,
			  size_t max, size_t size)
{
	int i;

	for (i = 0; i < max; i++) {
		dma_free_coherent(dev, size, ring[i]->buf, ring[i]->dma);
		kfree(ring[i]);
	}
	kfree(ring);
}

int srp_target_alloc(struct srp_target *target, struct device *dev,
		     size_t nr, size_t iu_size)
{
	int err;

	spin_lock_init(&target->lock);

	target->dev = dev;

	target->srp_iu_size = iu_size;
	target->rx_ring_size = nr;
	target->rx_ring = srp_ring_alloc(target->dev, nr, iu_size);
	if (!target->rx_ring)
		return -ENOMEM;
	err = srp_iu_pool_alloc(&target->iu_queue, nr, target->rx_ring);
	if (err)
		goto free_ring;

	dev_set_drvdata(target->dev, target);
	return 0;

free_ring:
	srp_ring_free(target->dev, target->rx_ring, nr, iu_size);
	return -ENOMEM;
}

void srp_target_free(struct srp_target *target)
{
	dev_set_drvdata(target->dev, NULL);
	srp_ring_free(target->dev, target->rx_ring, target->rx_ring_size,
		      target->srp_iu_size);
	srp_iu_pool_free(&target->iu_queue);
}

struct iu_entry *srp_iu_get(struct srp_target *target)
{
	struct iu_entry *iue = NULL;

	if (kfifo_out_locked(&target->iu_queue.queue, (void *)&iue,
			     sizeof(void *),
			     &target->iu_queue.lock) != sizeof(void *)) {
		WARN_ONCE(1, "unexpected fifo state");
		return NULL;
	}
	if (!iue)
		return iue;
	iue->target = target;
	iue->flags = 0;
	return iue;
}

void srp_iu_put(struct iu_entry *iue)
{
	kfifo_in_locked(&iue->target->iu_queue.queue, (void *)&iue,
			sizeof(void *), &iue->target->iu_queue.lock);
}

static int srp_direct_data(struct ibmvscsis_cmd *cmd, struct srp_direct_buf *md,
			   enum dma_data_direction dir, srp_rdma_t rdma_io,
			   int dma_map, int ext_desc)
{
	struct iu_entry *iue = NULL;
	struct scatterlist *sg = NULL;
	int err, nsg = 0, len;

	if (dma_map) {
		iue = cmd->iue;
		sg = cmd->se_cmd.t_data_sg;
		nsg = dma_map_sg(iue->target->dev, sg, cmd->se_cmd.t_data_nents,
				 DMA_BIDIRECTIONAL);
		if (!nsg) {
			pr_err("fail to map %p %d\n", iue,
			       cmd->se_cmd.t_data_nents);
			return 0;
		}
		len = min(cmd->se_cmd.data_length, be32_to_cpu(md->len));
	} else {
		len = be32_to_cpu(md->len);
	}

	err = rdma_io(cmd, sg, nsg, md, 1, dir, len);

	if (dma_map)
		dma_unmap_sg(iue->target->dev, sg, cmd->se_cmd.t_data_nents,
			     DMA_BIDIRECTIONAL);

	return err;
}

static int srp_indirect_data(struct ibmvscsis_cmd *cmd, struct srp_cmd *srp_cmd,
			     struct srp_indirect_buf *id,
			     enum dma_data_direction dir, srp_rdma_t rdma_io,
			     int dma_map, int ext_desc)
{
	struct iu_entry *iue = NULL;
	struct srp_direct_buf *md = NULL;
	struct scatterlist dummy, *sg = NULL;
	dma_addr_t token = 0;
	int err = 0;
	int nmd, nsg = 0, len;

	if (dma_map || ext_desc) {
		iue = cmd->iue;
		sg = cmd->se_cmd.t_data_sg;
	}

	nmd = be32_to_cpu(id->table_desc.len) / sizeof(struct srp_direct_buf);

	if ((dir == DMA_FROM_DEVICE && nmd == srp_cmd->data_in_desc_cnt) ||
	    (dir == DMA_TO_DEVICE && nmd == srp_cmd->data_out_desc_cnt)) {
		md = &id->desc_list[0];
		goto rdma;
	}

	if (ext_desc && dma_map) {
		md = dma_alloc_coherent(iue->target->dev,
					be32_to_cpu(id->table_desc.len),
					&token, GFP_KERNEL);
		if (!md) {
			pr_err("Can't get dma memory %u\n",
			       be32_to_cpu(id->table_desc.len));
			return -ENOMEM;
		}

		sg_init_one(&dummy, md, be32_to_cpu(id->table_desc.len));
		sg_dma_address(&dummy) = token;
		sg_dma_len(&dummy) = be32_to_cpu(id->table_desc.len);
		err = rdma_io(cmd, &dummy, 1, &id->table_desc, 1, DMA_TO_DEVICE,
			      be32_to_cpu(id->table_desc.len));
		if (err) {
			pr_err("Error copying indirect table %d\n", err);
			goto free_mem;
		}
	} else {
		pr_err("This command uses external indirect buffer\n");
		return -EINVAL;
	}

rdma:
	if (dma_map) {
		nsg = dma_map_sg(iue->target->dev, sg, cmd->se_cmd.t_data_nents,
				 DMA_BIDIRECTIONAL);
		if (!nsg) {
			pr_err("fail to map %p %d\n", iue,
			       cmd->se_cmd.t_data_nents);
			err = -EIO;
			goto free_mem;
		}
		len = min(cmd->se_cmd.data_length, be32_to_cpu(id->len));
	} else {
		len = be32_to_cpu(id->len);
	}

	err = rdma_io(cmd, sg, nsg, md, nmd, dir, len);

	if (dma_map)
		dma_unmap_sg(iue->target->dev, sg, cmd->se_cmd.t_data_nents,
			     DMA_BIDIRECTIONAL);

free_mem:
	if (token && dma_map) {
		dma_free_coherent(iue->target->dev,
				  be32_to_cpu(id->table_desc.len), md, token);
	}
	return err;
}

static int data_out_desc_size(struct srp_cmd *cmd)
{
	int size = 0;
	u8 fmt = cmd->buf_fmt >> 4;

	switch (fmt) {
	case SRP_NO_DATA_DESC:
		break;
	case SRP_DATA_DESC_DIRECT:
		size = sizeof(struct srp_direct_buf);
		break;
	case SRP_DATA_DESC_INDIRECT:
		size = sizeof(struct srp_indirect_buf) +
			sizeof(struct srp_direct_buf) * cmd->data_out_desc_cnt;
		break;
	default:
		pr_err("client error. Invalid data_out_format %x\n", fmt);
		break;
	}
	return size;
}

/*
 * TODO: this can be called multiple times for a single command if it
 * has very long data.
 */
int srp_transfer_data(struct ibmvscsis_cmd *cmd, struct srp_cmd *srp_cmd,
		      srp_rdma_t rdma_io, int dma_map, int ext_desc)
{
	struct srp_direct_buf *md;
	struct srp_indirect_buf *id;
	enum dma_data_direction dir;
	int offset, err = 0;
	u8 format;

	if (!cmd->se_cmd.t_data_nents)
		return 0;

	offset = srp_cmd->add_cdb_len & ~3;

	dir = srp_cmd_direction(srp_cmd);
	if (dir == DMA_FROM_DEVICE)
		offset += data_out_desc_size(srp_cmd);

	if (dir == DMA_TO_DEVICE)
		format = srp_cmd->buf_fmt >> 4;
	else
		format = srp_cmd->buf_fmt & ((1U << 4) - 1);

	switch (format) {
	case SRP_NO_DATA_DESC:
		break;
	case SRP_DATA_DESC_DIRECT:
		md = (struct srp_direct_buf *)(srp_cmd->add_data + offset);
		err = srp_direct_data(cmd, md, dir, rdma_io, dma_map, ext_desc);
		break;
	case SRP_DATA_DESC_INDIRECT:
		id = (struct srp_indirect_buf *)(srp_cmd->add_data + offset);
		err = srp_indirect_data(cmd, srp_cmd, id, dir, rdma_io, dma_map,
					ext_desc);
		break;
	default:
		pr_err("Unknown format %d %x\n", dir, format);
		err = -EINVAL;
	}

	return err;
}

u64 srp_data_length(struct srp_cmd *cmd, enum dma_data_direction dir)
{
	struct srp_direct_buf *md;
	struct srp_indirect_buf *id;
	u64 len = 0;
	uint offset = cmd->add_cdb_len & ~3;
	u8 fmt;

	if (dir == DMA_TO_DEVICE) {
		fmt = cmd->buf_fmt >> 4;
	} else {
		fmt = cmd->buf_fmt & ((1U << 4) - 1);
		offset += data_out_desc_size(cmd);
	}

	switch (fmt) {
	case SRP_NO_DATA_DESC:
		break;
	case SRP_DATA_DESC_DIRECT:
		md = (struct srp_direct_buf *)(cmd->add_data + offset);
		len = be32_to_cpu(md->len);
		break;
	case SRP_DATA_DESC_INDIRECT:
		id = (struct srp_indirect_buf *)(cmd->add_data + offset);
		len = be32_to_cpu(id->len);
		break;
	default:
		pr_err("invalid data format %x\n", fmt);
		break;
	}
	return len;
}

int srp_get_desc_table(struct srp_cmd *srp_cmd, enum dma_data_direction *dir,
		       u64 *data_len)
{
	struct srp_indirect_buf *idb;
	struct srp_direct_buf *db;
	uint add_cdb_offset;
	int rc;

	/*
	 * The pointer computations below will only be compiled correctly
	 * if srp_cmd::add_data is declared as s8*, u8*, s8[] or u8[], so check
	 * whether srp_cmd::add_data has been declared as a byte pointer.
	 */
	BUILD_BUG_ON(!__same_type(srp_cmd->add_data[0], (s8)0)
		     && !__same_type(srp_cmd->add_data[0], (u8)0));

	BUG_ON(!dir);
	BUG_ON(!data_len);

	rc = 0;
	*data_len = 0;

	*dir = DMA_NONE;

	if (srp_cmd->buf_fmt & 0xf)
		*dir = DMA_FROM_DEVICE;
	else if (srp_cmd->buf_fmt >> 4)
		*dir = DMA_TO_DEVICE;

	add_cdb_offset = srp_cmd->add_cdb_len & ~3;
	if (((srp_cmd->buf_fmt & 0xf) == SRP_DATA_DESC_DIRECT) ||
	    ((srp_cmd->buf_fmt >> 4) == SRP_DATA_DESC_DIRECT)) {
		db = (struct srp_direct_buf *)(srp_cmd->add_data
					       + add_cdb_offset);
		*data_len = be32_to_cpu(db->len);
	} else if (((srp_cmd->buf_fmt & 0xf) == SRP_DATA_DESC_INDIRECT) ||
		   ((srp_cmd->buf_fmt >> 4) == SRP_DATA_DESC_INDIRECT)) {
		idb = (struct srp_indirect_buf *)(srp_cmd->add_data
						  + add_cdb_offset);

		*data_len = be32_to_cpu(idb->len);
	}
	return rc;
}

MODULE_DESCRIPTION("SCSI RDMA Protocol lib functions");
MODULE_AUTHOR("FUJITA Tomonori");
MODULE_LICENSE("GPL");
