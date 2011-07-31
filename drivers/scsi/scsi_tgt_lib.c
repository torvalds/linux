/*
 * SCSI target lib functions
 *
 * Copyright (C) 2005 Mike Christie <michaelc@cs.wisc.edu>
 * Copyright (C) 2005 FUJITA Tomonori <tomof@acm.org>
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
#include <linux/blkdev.h>
#include <linux/hash.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_tgt.h>

#include "scsi_tgt_priv.h"

static struct workqueue_struct *scsi_tgtd;
static struct kmem_cache *scsi_tgt_cmd_cache;

/*
 * TODO: this struct will be killed when the block layer supports large bios
 * and James's work struct code is in
 */
struct scsi_tgt_cmd {
	/* TODO replace work with James b's code */
	struct work_struct work;
	/* TODO fix limits of some drivers */
	struct bio *bio;

	struct list_head hash_list;
	struct request *rq;
	u64 itn_id;
	u64 tag;
};

#define TGT_HASH_ORDER	4
#define cmd_hashfn(tag)	hash_long((unsigned long) (tag), TGT_HASH_ORDER)

struct scsi_tgt_queuedata {
	struct Scsi_Host *shost;
	struct list_head cmd_hash[1 << TGT_HASH_ORDER];
	spinlock_t cmd_hash_lock;
};

/*
 * Function:	scsi_host_get_command()
 *
 * Purpose:	Allocate and setup a scsi command block and blk request
 *
 * Arguments:	shost	- scsi host
 *		data_dir - dma data dir
 *		gfp_mask- allocator flags
 *
 * Returns:	The allocated scsi command structure.
 *
 * This should be called by target LLDs to get a command.
 */
struct scsi_cmnd *scsi_host_get_command(struct Scsi_Host *shost,
					enum dma_data_direction data_dir,
					gfp_t gfp_mask)
{
	int write = (data_dir == DMA_TO_DEVICE);
	struct request *rq;
	struct scsi_cmnd *cmd;
	struct scsi_tgt_cmd *tcmd;

	/* Bail if we can't get a reference to the device */
	if (!get_device(&shost->shost_gendev))
		return NULL;

	tcmd = kmem_cache_alloc(scsi_tgt_cmd_cache, GFP_ATOMIC);
	if (!tcmd)
		goto put_dev;

	/*
	 * The blk helpers are used to the READ/WRITE requests
	 * transfering data from a initiator point of view. Since
	 * we are in target mode we want the opposite.
	 */
	rq = blk_get_request(shost->uspace_req_q, !write, gfp_mask);
	if (!rq)
		goto free_tcmd;

	cmd = __scsi_get_command(shost, gfp_mask);
	if (!cmd)
		goto release_rq;

	cmd->sc_data_direction = data_dir;
	cmd->jiffies_at_alloc = jiffies;
	cmd->request = rq;

	cmd->cmnd = rq->cmd;

	rq->special = cmd;
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd_flags |= REQ_TYPE_BLOCK_PC;
	rq->end_io_data = tcmd;

	tcmd->rq = rq;

	return cmd;

release_rq:
	blk_put_request(rq);
free_tcmd:
	kmem_cache_free(scsi_tgt_cmd_cache, tcmd);
put_dev:
	put_device(&shost->shost_gendev);
	return NULL;

}
EXPORT_SYMBOL_GPL(scsi_host_get_command);

/*
 * Function:	scsi_host_put_command()
 *
 * Purpose:	Free a scsi command block
 *
 * Arguments:	shost	- scsi host
 * 		cmd	- command block to free
 *
 * Returns:	Nothing.
 *
 * Notes:	The command must not belong to any lists.
 */
void scsi_host_put_command(struct Scsi_Host *shost, struct scsi_cmnd *cmd)
{
	struct request_queue *q = shost->uspace_req_q;
	struct request *rq = cmd->request;
	struct scsi_tgt_cmd *tcmd = rq->end_io_data;
	unsigned long flags;

	kmem_cache_free(scsi_tgt_cmd_cache, tcmd);

	spin_lock_irqsave(q->queue_lock, flags);
	__blk_put_request(q, rq);
	spin_unlock_irqrestore(q->queue_lock, flags);

	__scsi_put_command(shost, cmd, &shost->shost_gendev);
}
EXPORT_SYMBOL_GPL(scsi_host_put_command);

static void cmd_hashlist_del(struct scsi_cmnd *cmd)
{
	struct request_queue *q = cmd->request->q;
	struct scsi_tgt_queuedata *qdata = q->queuedata;
	unsigned long flags;
	struct scsi_tgt_cmd *tcmd = cmd->request->end_io_data;

	spin_lock_irqsave(&qdata->cmd_hash_lock, flags);
	list_del(&tcmd->hash_list);
	spin_unlock_irqrestore(&qdata->cmd_hash_lock, flags);
}

static void scsi_unmap_user_pages(struct scsi_tgt_cmd *tcmd)
{
	blk_rq_unmap_user(tcmd->bio);
}

static void scsi_tgt_cmd_destroy(struct work_struct *work)
{
	struct scsi_tgt_cmd *tcmd =
		container_of(work, struct scsi_tgt_cmd, work);
	struct scsi_cmnd *cmd = tcmd->rq->special;

	dprintk("cmd %p %d %u\n", cmd, cmd->sc_data_direction,
		rq_data_dir(cmd->request));
	scsi_unmap_user_pages(tcmd);
	tcmd->rq->bio = NULL;
	scsi_host_put_command(scsi_tgt_cmd_to_host(cmd), cmd);
}

static void init_scsi_tgt_cmd(struct request *rq, struct scsi_tgt_cmd *tcmd,
			      u64 itn_id, u64 tag)
{
	struct scsi_tgt_queuedata *qdata = rq->q->queuedata;
	unsigned long flags;
	struct list_head *head;

	tcmd->itn_id = itn_id;
	tcmd->tag = tag;
	tcmd->bio = NULL;
	INIT_WORK(&tcmd->work, scsi_tgt_cmd_destroy);
	spin_lock_irqsave(&qdata->cmd_hash_lock, flags);
	head = &qdata->cmd_hash[cmd_hashfn(tag)];
	list_add(&tcmd->hash_list, head);
	spin_unlock_irqrestore(&qdata->cmd_hash_lock, flags);
}

/*
 * scsi_tgt_alloc_queue - setup queue used for message passing
 * shost: scsi host
 *
 * This should be called by the LLD after host allocation.
 * And will be released when the host is released.
 */
int scsi_tgt_alloc_queue(struct Scsi_Host *shost)
{
	struct scsi_tgt_queuedata *queuedata;
	struct request_queue *q;
	int err, i;

	/*
	 * Do we need to send a netlink event or should uspace
	 * just respond to the hotplug event?
	 */
	q = __scsi_alloc_queue(shost, NULL);
	if (!q)
		return -ENOMEM;

	queuedata = kzalloc(sizeof(*queuedata), GFP_KERNEL);
	if (!queuedata) {
		err = -ENOMEM;
		goto cleanup_queue;
	}
	queuedata->shost = shost;
	q->queuedata = queuedata;

	/*
	 * this is a silly hack. We should probably just queue as many
	 * command as is recvd to userspace. uspace can then make
	 * sure we do not overload the HBA
	 */
	q->nr_requests = shost->can_queue;
	/*
	 * We currently only support software LLDs so this does
	 * not matter for now. Do we need this for the cards we support?
	 * If so we should make it a host template value.
	 */
	blk_queue_dma_alignment(q, 0);
	shost->uspace_req_q = q;

	for (i = 0; i < ARRAY_SIZE(queuedata->cmd_hash); i++)
		INIT_LIST_HEAD(&queuedata->cmd_hash[i]);
	spin_lock_init(&queuedata->cmd_hash_lock);

	return 0;

cleanup_queue:
	blk_cleanup_queue(q);
	return err;
}
EXPORT_SYMBOL_GPL(scsi_tgt_alloc_queue);

void scsi_tgt_free_queue(struct Scsi_Host *shost)
{
	int i;
	unsigned long flags;
	struct request_queue *q = shost->uspace_req_q;
	struct scsi_cmnd *cmd;
	struct scsi_tgt_queuedata *qdata = q->queuedata;
	struct scsi_tgt_cmd *tcmd, *n;
	LIST_HEAD(cmds);

	spin_lock_irqsave(&qdata->cmd_hash_lock, flags);

	for (i = 0; i < ARRAY_SIZE(qdata->cmd_hash); i++) {
		list_for_each_entry_safe(tcmd, n, &qdata->cmd_hash[i],
					 hash_list) {
			list_del(&tcmd->hash_list);
			list_add(&tcmd->hash_list, &cmds);
		}
	}

	spin_unlock_irqrestore(&qdata->cmd_hash_lock, flags);

	while (!list_empty(&cmds)) {
		tcmd = list_entry(cmds.next, struct scsi_tgt_cmd, hash_list);
		list_del(&tcmd->hash_list);
		cmd = tcmd->rq->special;

		shost->hostt->eh_abort_handler(cmd);
		scsi_tgt_cmd_destroy(&tcmd->work);
	}
}
EXPORT_SYMBOL_GPL(scsi_tgt_free_queue);

struct Scsi_Host *scsi_tgt_cmd_to_host(struct scsi_cmnd *cmd)
{
	struct scsi_tgt_queuedata *queue = cmd->request->q->queuedata;
	return queue->shost;
}
EXPORT_SYMBOL_GPL(scsi_tgt_cmd_to_host);

/*
 * scsi_tgt_queue_command - queue command for userspace processing
 * @cmd:	scsi command
 * @scsilun:	scsi lun
 * @tag:	unique value to identify this command for tmf
 */
int scsi_tgt_queue_command(struct scsi_cmnd *cmd, u64 itn_id,
			   struct scsi_lun *scsilun, u64 tag)
{
	struct scsi_tgt_cmd *tcmd = cmd->request->end_io_data;
	int err;

	init_scsi_tgt_cmd(cmd->request, tcmd, itn_id, tag);
	err = scsi_tgt_uspace_send_cmd(cmd, itn_id, scsilun, tag);
	if (err)
		cmd_hashlist_del(cmd);

	return err;
}
EXPORT_SYMBOL_GPL(scsi_tgt_queue_command);

/*
 * This is run from a interrupt handler normally and the unmap
 * needs process context so we must queue
 */
static void scsi_tgt_cmd_done(struct scsi_cmnd *cmd)
{
	struct scsi_tgt_cmd *tcmd = cmd->request->end_io_data;

	dprintk("cmd %p %u\n", cmd, rq_data_dir(cmd->request));

	scsi_tgt_uspace_send_status(cmd, tcmd->itn_id, tcmd->tag);

	scsi_release_buffers(cmd);

	queue_work(scsi_tgtd, &tcmd->work);
}

static int scsi_tgt_transfer_response(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *shost = scsi_tgt_cmd_to_host(cmd);
	int err;

	dprintk("cmd %p %u\n", cmd, rq_data_dir(cmd->request));

	err = shost->hostt->transfer_response(cmd, scsi_tgt_cmd_done);
	switch (err) {
	case SCSI_MLQUEUE_HOST_BUSY:
	case SCSI_MLQUEUE_DEVICE_BUSY:
		return -EAGAIN;
	}
	return 0;
}

/* TODO: test this crap and replace bio_map_user with new interface maybe */
static int scsi_map_user_pages(struct scsi_tgt_cmd *tcmd, struct scsi_cmnd *cmd,
			       unsigned long uaddr, unsigned int len, int rw)
{
	struct request_queue *q = cmd->request->q;
	struct request *rq = cmd->request;
	int err;

	dprintk("%lx %u\n", uaddr, len);
	err = blk_rq_map_user(q, rq, NULL, (void *)uaddr, len, GFP_KERNEL);
	if (err) {
		/*
		 * TODO: need to fixup sg_tablesize, max_segment_size,
		 * max_sectors, etc for modern HW and software drivers
		 * where this value is bogus.
		 *
		 * TODO2: we can alloc a reserve buffer of max size
		 * we can handle and do the slow copy path for really large
		 * IO.
		 */
		eprintk("Could not handle request of size %u.\n", len);
		return err;
	}

	tcmd->bio = rq->bio;
	err = scsi_init_io(cmd, GFP_KERNEL);
	if (err) {
		scsi_release_buffers(cmd);
		goto unmap_rq;
	}
	/*
	 * we use REQ_TYPE_BLOCK_PC so scsi_init_io doesn't set the
	 * length for us.
	 */
	cmd->sdb.length = blk_rq_bytes(rq);

	return 0;

unmap_rq:
	scsi_unmap_user_pages(tcmd);
	return err;
}

static int scsi_tgt_copy_sense(struct scsi_cmnd *cmd, unsigned long uaddr,
				unsigned len)
{
	char __user *p = (char __user *) uaddr;

	if (copy_from_user(cmd->sense_buffer, p,
			   min_t(unsigned, SCSI_SENSE_BUFFERSIZE, len))) {
		printk(KERN_ERR "Could not copy the sense buffer\n");
		return -EIO;
	}
	return 0;
}

static int scsi_tgt_abort_cmd(struct Scsi_Host *shost, struct scsi_cmnd *cmd)
{
	struct scsi_tgt_cmd *tcmd;
	int err;

	err = shost->hostt->eh_abort_handler(cmd);
	if (err)
		eprintk("fail to abort %p\n", cmd);

	tcmd = cmd->request->end_io_data;
	scsi_tgt_cmd_destroy(&tcmd->work);
	return err;
}

static struct request *tgt_cmd_hash_lookup(struct request_queue *q, u64 tag)
{
	struct scsi_tgt_queuedata *qdata = q->queuedata;
	struct request *rq = NULL;
	struct list_head *head;
	struct scsi_tgt_cmd *tcmd;
	unsigned long flags;

	head = &qdata->cmd_hash[cmd_hashfn(tag)];
	spin_lock_irqsave(&qdata->cmd_hash_lock, flags);
	list_for_each_entry(tcmd, head, hash_list) {
		if (tcmd->tag == tag) {
			rq = tcmd->rq;
			list_del(&tcmd->hash_list);
			break;
		}
	}
	spin_unlock_irqrestore(&qdata->cmd_hash_lock, flags);

	return rq;
}

int scsi_tgt_kspace_exec(int host_no, u64 itn_id, int result, u64 tag,
			 unsigned long uaddr, u32 len, unsigned long sense_uaddr,
			 u32 sense_len, u8 rw)
{
	struct Scsi_Host *shost;
	struct scsi_cmnd *cmd;
	struct request *rq;
	struct scsi_tgt_cmd *tcmd;
	int err = 0;

	dprintk("%d %llu %d %u %lx %u\n", host_no, (unsigned long long) tag,
		result, len, uaddr, rw);

	/* TODO: replace with a O(1) alg */
	shost = scsi_host_lookup(host_no);
	if (!shost) {
		printk(KERN_ERR "Could not find host no %d\n", host_no);
		return -EINVAL;
	}

	if (!shost->uspace_req_q) {
		printk(KERN_ERR "Not target scsi host %d\n", host_no);
		goto done;
	}

	rq = tgt_cmd_hash_lookup(shost->uspace_req_q, tag);
	if (!rq) {
		printk(KERN_ERR "Could not find tag %llu\n",
		       (unsigned long long) tag);
		err = -EINVAL;
		goto done;
	}
	cmd = rq->special;

	dprintk("cmd %p scb %x result %d len %d bufflen %u %u %x\n",
		cmd, cmd->cmnd[0], result, len, scsi_bufflen(cmd),
		rq_data_dir(rq), cmd->cmnd[0]);

	if (result == TASK_ABORTED) {
		scsi_tgt_abort_cmd(shost, cmd);
		goto done;
	}
	/*
	 * store the userspace values here, the working values are
	 * in the request_* values
	 */
	tcmd = cmd->request->end_io_data;
	cmd->result = result;

	if (cmd->result == SAM_STAT_CHECK_CONDITION)
		scsi_tgt_copy_sense(cmd, sense_uaddr, sense_len);

	if (len) {
		err = scsi_map_user_pages(rq->end_io_data, cmd, uaddr, len, rw);
		if (err) {
			/*
			 * user-space daemon bugs or OOM
			 * TODO: we can do better for OOM.
			 */
			struct scsi_tgt_queuedata *qdata;
			struct list_head *head;
			unsigned long flags;

			eprintk("cmd %p ret %d uaddr %lx len %d rw %d\n",
				cmd, err, uaddr, len, rw);

			qdata = shost->uspace_req_q->queuedata;
			head = &qdata->cmd_hash[cmd_hashfn(tcmd->tag)];

			spin_lock_irqsave(&qdata->cmd_hash_lock, flags);
			list_add(&tcmd->hash_list, head);
			spin_unlock_irqrestore(&qdata->cmd_hash_lock, flags);

			goto done;
		}
	}
	err = scsi_tgt_transfer_response(cmd);
done:
	scsi_host_put(shost);
	return err;
}

int scsi_tgt_tsk_mgmt_request(struct Scsi_Host *shost, u64 itn_id,
			      int function, u64 tag, struct scsi_lun *scsilun,
			      void *data)
{
	int err;

	/* TODO: need to retry if this fails. */
	err = scsi_tgt_uspace_send_tsk_mgmt(shost->host_no, itn_id,
					    function, tag, scsilun, data);
	if (err < 0)
		eprintk("The task management request lost!\n");
	return err;
}
EXPORT_SYMBOL_GPL(scsi_tgt_tsk_mgmt_request);

int scsi_tgt_kspace_tsk_mgmt(int host_no, u64 itn_id, u64 mid, int result)
{
	struct Scsi_Host *shost;
	int err = -EINVAL;

	dprintk("%d %d %llx\n", host_no, result, (unsigned long long) mid);

	shost = scsi_host_lookup(host_no);
	if (!shost) {
		printk(KERN_ERR "Could not find host no %d\n", host_no);
		return err;
	}

	if (!shost->uspace_req_q) {
		printk(KERN_ERR "Not target scsi host %d\n", host_no);
		goto done;
	}

	err = shost->transportt->tsk_mgmt_response(shost, itn_id, mid, result);
done:
	scsi_host_put(shost);
	return err;
}

int scsi_tgt_it_nexus_create(struct Scsi_Host *shost, u64 itn_id,
			     char *initiator)
{
	int err;

	/* TODO: need to retry if this fails. */
	err = scsi_tgt_uspace_send_it_nexus_request(shost->host_no, itn_id, 0,
						    initiator);
	if (err < 0)
		eprintk("The i_t_neuxs request lost, %d %llx!\n",
			shost->host_no, (unsigned long long)itn_id);
	return err;
}
EXPORT_SYMBOL_GPL(scsi_tgt_it_nexus_create);

int scsi_tgt_it_nexus_destroy(struct Scsi_Host *shost, u64 itn_id)
{
	int err;

	/* TODO: need to retry if this fails. */
	err = scsi_tgt_uspace_send_it_nexus_request(shost->host_no,
						    itn_id, 1, NULL);
	if (err < 0)
		eprintk("The i_t_neuxs request lost, %d %llx!\n",
			shost->host_no, (unsigned long long)itn_id);
	return err;
}
EXPORT_SYMBOL_GPL(scsi_tgt_it_nexus_destroy);

int scsi_tgt_kspace_it_nexus_rsp(int host_no, u64 itn_id, int result)
{
	struct Scsi_Host *shost;
	int err = -EINVAL;

	dprintk("%d %d%llx\n", host_no, result, (unsigned long long)itn_id);

	shost = scsi_host_lookup(host_no);
	if (!shost) {
		printk(KERN_ERR "Could not find host no %d\n", host_no);
		return err;
	}

	if (!shost->uspace_req_q) {
		printk(KERN_ERR "Not target scsi host %d\n", host_no);
		goto done;
	}

	err = shost->transportt->it_nexus_response(shost, itn_id, result);
done:
	scsi_host_put(shost);
	return err;
}

static int __init scsi_tgt_init(void)
{
	int err;

	scsi_tgt_cmd_cache =  KMEM_CACHE(scsi_tgt_cmd, 0);
	if (!scsi_tgt_cmd_cache)
		return -ENOMEM;

	scsi_tgtd = create_workqueue("scsi_tgtd");
	if (!scsi_tgtd) {
		err = -ENOMEM;
		goto free_kmemcache;
	}

	err = scsi_tgt_if_init();
	if (err)
		goto destroy_wq;

	return 0;

destroy_wq:
	destroy_workqueue(scsi_tgtd);
free_kmemcache:
	kmem_cache_destroy(scsi_tgt_cmd_cache);
	return err;
}

static void __exit scsi_tgt_exit(void)
{
	destroy_workqueue(scsi_tgtd);
	scsi_tgt_if_exit();
	kmem_cache_destroy(scsi_tgt_cmd_cache);
}

module_init(scsi_tgt_init);
module_exit(scsi_tgt_exit);

MODULE_DESCRIPTION("SCSI target core");
MODULE_LICENSE("GPL");
