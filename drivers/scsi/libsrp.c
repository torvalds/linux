/*
 * SCSI RDMA Protocol lib functions
 *
 * Copyright (C) 2006 FUJITA Tomonori <tomof@acm.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_tgt.h>
#include <scsi/srp.h>
#include <scsi/libsrp.h>

enum srp_task_attributes {
	SRP_SIMPLE_TASK = 0,
	SRP_HEAD_TASK = 1,
	SRP_ORDERED_TASK = 2,
	SRP_ACA_TASK = 4
};

/* tmp - will replace with SCSI logging stuff */
#define eprintk(fmt, args...)					\
do {								\
	printk("%s(%d) " fmt, __func__, __LINE__, ##args);	\
} while (0)
/* #define dprintk eprintk */
#define dprintk(fmt, args...)

static int srp_iu_pool_alloc(struct srp_queue *q, size_t max,
			     struct srp_buf **ring)
{
	int i;
	struct iu_entry *iue;

	q->pool = kcalloc(max, sizeof(struct iu_entry *), GFP_KERNEL);
	if (!q->pool)
		return -ENOMEM;
	q->items = kcalloc(max, sizeof(struct iu_entry), GFP_KERNEL);
	if (!q->items)
		goto free_pool;

	spin_lock_init(&q->lock);
	kfifo_init(&q->queue, (void *) q->pool, max * sizeof(void *));

	for (i = 0, iue = q->items; i < max; i++) {
		kfifo_in(&q->queue, (void *) &iue, sizeof(void *));
		iue->sbuf = ring[i];
		iue++;
	}
	return 0;

	kfree(q->items);
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
	int i;
	struct srp_buf **ring;

	ring = kcalloc(max, sizeof(struct srp_buf *), GFP_KERNEL);
	if (!ring)
		return NULL;

	for (i = 0; i < max; i++) {
		ring[i] = kzalloc(sizeof(struct srp_buf), GFP_KERNEL);
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
		if (ring[i]->buf)
			dma_free_coherent(dev, size, ring[i]->buf, ring[i]->dma);
		kfree(ring[i]);
	}
	kfree(ring);

	return NULL;
}

static void srp_ring_free(struct device *dev, struct srp_buf **ring, size_t max,
			  size_t size)
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
	INIT_LIST_HEAD(&target->cmd_queue);

	target->dev = dev;
	dev_set_drvdata(target->dev, target);

	target->srp_iu_size = iu_size;
	target->rx_ring_size = nr;
	target->rx_ring = srp_ring_alloc(target->dev, nr, iu_size);
	if (!target->rx_ring)
		return -ENOMEM;
	err = srp_iu_pool_alloc(&target->iu_queue, nr, target->rx_ring);
	if (err)
		goto free_ring;

	return 0;

free_ring:
	srp_ring_free(target->dev, target->rx_ring, nr, iu_size);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(srp_target_alloc);

void srp_target_free(struct srp_target *target)
{
	srp_ring_free(target->dev, target->rx_ring, target->rx_ring_size,
		      target->srp_iu_size);
	srp_iu_pool_free(&target->iu_queue);
}
EXPORT_SYMBOL_GPL(srp_target_free);

struct iu_entry *srp_iu_get(struct srp_target *target)
{
	struct iu_entry *iue = NULL;

	if (kfifo_out_locked(&target->iu_queue.queue, (void *) &iue,
		sizeof(void *), &target->iu_queue.lock) != sizeof(void *)) {
			WARN_ONCE(1, "unexpected fifo state");
			return NULL;
	}
	if (!iue)
		return iue;
	iue->target = target;
	INIT_LIST_HEAD(&iue->ilist);
	iue->flags = 0;
	return iue;
}
EXPORT_SYMBOL_GPL(srp_iu_get);

void srp_iu_put(struct iu_entry *iue)
{
	kfifo_in_locked(&iue->target->iu_queue.queue, (void *) &iue,
			sizeof(void *), &iue->target->iu_queue.lock);
}
EXPORT_SYMBOL_GPL(srp_iu_put);

static int srp_direct_data(struct scsi_cmnd *sc, struct srp_direct_buf *md,
			   enum dma_data_direction dir, srp_rdma_t rdma_io,
			   int dma_map, int ext_desc)
{
	struct iu_entry *iue = NULL;
	struct scatterlist *sg = NULL;
	int err, nsg = 0, len;

	if (dma_map) {
		iue = (struct iu_entry *) sc->SCp.ptr;
		sg = scsi_sglist(sc);

		dprintk("%p %u %u %d\n", iue, scsi_bufflen(sc),
			md->len, scsi_sg_count(sc));

		nsg = dma_map_sg(iue->target->dev, sg, scsi_sg_count(sc),
				 DMA_BIDIRECTIONAL);
		if (!nsg) {
			printk("fail to map %p %d\n", iue, scsi_sg_count(sc));
			return 0;
		}
		len = min(scsi_bufflen(sc), md->len);
	} else
		len = md->len;

	err = rdma_io(sc, sg, nsg, md, 1, dir, len);

	if (dma_map)
		dma_unmap_sg(iue->target->dev, sg, nsg, DMA_BIDIRECTIONAL);

	return err;
}

static int srp_indirect_data(struct scsi_cmnd *sc, struct srp_cmd *cmd,
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
		iue = (struct iu_entry *) sc->SCp.ptr;
		sg = scsi_sglist(sc);

		dprintk("%p %u %u %d %d\n",
			iue, scsi_bufflen(sc), id->len,
			cmd->data_in_desc_cnt, cmd->data_out_desc_cnt);
	}

	nmd = id->table_desc.len / sizeof(struct srp_direct_buf);

	if ((dir == DMA_FROM_DEVICE && nmd == cmd->data_in_desc_cnt) ||
	    (dir == DMA_TO_DEVICE && nmd == cmd->data_out_desc_cnt)) {
		md = &id->desc_list[0];
		goto rdma;
	}

	if (ext_desc && dma_map) {
		md = dma_alloc_coherent(iue->target->dev, id->table_desc.len,
				&token, GFP_KERNEL);
		if (!md) {
			eprintk("Can't get dma memory %u\n", id->table_desc.len);
			return -ENOMEM;
		}

		sg_init_one(&dummy, md, id->table_desc.len);
		sg_dma_address(&dummy) = token;
		sg_dma_len(&dummy) = id->table_desc.len;
		err = rdma_io(sc, &dummy, 1, &id->table_desc, 1, DMA_TO_DEVICE,
			      id->table_desc.len);
		if (err) {
			eprintk("Error copying indirect table %d\n", err);
			goto free_mem;
		}
	} else {
		eprintk("This command uses external indirect buffer\n");
		return -EINVAL;
	}

rdma:
	if (dma_map) {
		nsg = dma_map_sg(iue->target->dev, sg, scsi_sg_count(sc),
				 DMA_BIDIRECTIONAL);
		if (!nsg) {
			eprintk("fail to map %p %d\n", iue, scsi_sg_count(sc));
			err = -EIO;
			goto free_mem;
		}
		len = min(scsi_bufflen(sc), id->len);
	} else
		len = id->len;

	err = rdma_io(sc, sg, nsg, md, nmd, dir, len);

	if (dma_map)
		dma_unmap_sg(iue->target->dev, sg, nsg, DMA_BIDIRECTIONAL);

free_mem:
	if (token && dma_map)
		dma_free_coherent(iue->target->dev, id->table_desc.len, md, token);

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
		eprintk("client error. Invalid data_out_format %x\n", fmt);
		break;
	}
	return size;
}

/*
 * TODO: this can be called multiple times for a single command if it
 * has very long data.
 */
int srp_transfer_data(struct scsi_cmnd *sc, struct srp_cmd *cmd,
		      srp_rdma_t rdma_io, int dma_map, int ext_desc)
{
	struct srp_direct_buf *md;
	struct srp_indirect_buf *id;
	enum dma_data_direction dir;
	int offset, err = 0;
	u8 format;

	offset = cmd->add_cdb_len & ~3;

	dir = srp_cmd_direction(cmd);
	if (dir == DMA_FROM_DEVICE)
		offset += data_out_desc_size(cmd);

	if (dir == DMA_TO_DEVICE)
		format = cmd->buf_fmt >> 4;
	else
		format = cmd->buf_fmt & ((1U << 4) - 1);

	switch (format) {
	case SRP_NO_DATA_DESC:
		break;
	case SRP_DATA_DESC_DIRECT:
		md = (struct srp_direct_buf *)
			(cmd->add_data + offset);
		err = srp_direct_data(sc, md, dir, rdma_io, dma_map, ext_desc);
		break;
	case SRP_DATA_DESC_INDIRECT:
		id = (struct srp_indirect_buf *)
			(cmd->add_data + offset);
		err = srp_indirect_data(sc, cmd, id, dir, rdma_io, dma_map,
					ext_desc);
		break;
	default:
		eprintk("Unknown format %d %x\n", dir, format);
		err = -EINVAL;
	}

	return err;
}
EXPORT_SYMBOL_GPL(srp_transfer_data);

static int vscsis_data_length(struct srp_cmd *cmd, enum dma_data_direction dir)
{
	struct srp_direct_buf *md;
	struct srp_indirect_buf *id;
	int len = 0, offset = cmd->add_cdb_len & ~3;
	u8 fmt;

	if (dir == DMA_TO_DEVICE)
		fmt = cmd->buf_fmt >> 4;
	else {
		fmt = cmd->buf_fmt & ((1U << 4) - 1);
		offset += data_out_desc_size(cmd);
	}

	switch (fmt) {
	case SRP_NO_DATA_DESC:
		break;
	case SRP_DATA_DESC_DIRECT:
		md = (struct srp_direct_buf *) (cmd->add_data + offset);
		len = md->len;
		break;
	case SRP_DATA_DESC_INDIRECT:
		id = (struct srp_indirect_buf *) (cmd->add_data + offset);
		len = id->len;
		break;
	default:
		eprintk("invalid data format %x\n", fmt);
		break;
	}
	return len;
}

int srp_cmd_queue(struct Scsi_Host *shost, struct srp_cmd *cmd, void *info,
		  u64 itn_id, u64 addr)
{
	enum dma_data_direction dir;
	struct scsi_cmnd *sc;
	int tag, len, err;

	switch (cmd->task_attr) {
	case SRP_SIMPLE_TASK:
		tag = MSG_SIMPLE_TAG;
		break;
	case SRP_ORDERED_TASK:
		tag = MSG_ORDERED_TAG;
		break;
	case SRP_HEAD_TASK:
		tag = MSG_HEAD_TAG;
		break;
	default:
		eprintk("Task attribute %d not supported\n", cmd->task_attr);
		tag = MSG_ORDERED_TAG;
	}

	dir = srp_cmd_direction(cmd);
	len = vscsis_data_length(cmd, dir);

	dprintk("%p %x %lx %d %d %d %llx\n", info, cmd->cdb[0],
		cmd->lun, dir, len, tag, (unsigned long long) cmd->tag);

	sc = scsi_host_get_command(shost, dir, GFP_KERNEL);
	if (!sc)
		return -ENOMEM;

	sc->SCp.ptr = info;
	memcpy(sc->cmnd, cmd->cdb, MAX_COMMAND_SIZE);
	sc->sdb.length = len;
	sc->sdb.table.sgl = (void *) (unsigned long) addr;
	sc->tag = tag;
	err = scsi_tgt_queue_command(sc, itn_id, (struct scsi_lun *)&cmd->lun,
				     cmd->tag);
	if (err)
		scsi_host_put_command(shost, sc);

	return err;
}
EXPORT_SYMBOL_GPL(srp_cmd_queue);

MODULE_DESCRIPTION("SCSI RDMA Protocol lib functions");
MODULE_AUTHOR("FUJITA Tomonori");
MODULE_LICENSE("GPL");
