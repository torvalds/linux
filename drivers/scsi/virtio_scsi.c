/*
 * Virtio SCSI HBA driver
 *
 * Copyright IBM Corp. 2010
 * Copyright Red Hat, Inc. 2011
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@linux.vnet.ibm.com>
 *  Paolo Bonzini   <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mempool.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>

#define VIRTIO_SCSI_MEMPOOL_SZ 64
#define VIRTIO_SCSI_EVENT_LEN 8

/* Command queue element */
struct virtio_scsi_cmd {
	struct scsi_cmnd *sc;
	struct completion *comp;
	union {
		struct virtio_scsi_cmd_req       cmd;
		struct virtio_scsi_ctrl_tmf_req  tmf;
		struct virtio_scsi_ctrl_an_req   an;
	} req;
	union {
		struct virtio_scsi_cmd_resp      cmd;
		struct virtio_scsi_ctrl_tmf_resp tmf;
		struct virtio_scsi_ctrl_an_resp  an;
		struct virtio_scsi_event         evt;
	} resp;
} ____cacheline_aligned_in_smp;

struct virtio_scsi_event_node {
	struct virtio_scsi *vscsi;
	struct virtio_scsi_event event;
	struct work_struct work;
};

struct virtio_scsi_vq {
	/* Protects vq */
	spinlock_t vq_lock;

	struct virtqueue *vq;
};

/* Per-target queue state */
struct virtio_scsi_target_state {
	/* Protects sg.  Lock hierarchy is tgt_lock -> vq_lock.  */
	spinlock_t tgt_lock;

	/* For sglist construction when adding commands to the virtqueue.  */
	struct scatterlist sg[];
};

/* Driver instance state */
struct virtio_scsi {
	struct virtio_device *vdev;

	struct virtio_scsi_vq ctrl_vq;
	struct virtio_scsi_vq event_vq;
	struct virtio_scsi_vq req_vq;

	/* Get some buffers ready for event vq */
	struct virtio_scsi_event_node event_list[VIRTIO_SCSI_EVENT_LEN];

	struct virtio_scsi_target_state *tgt[];
};

static struct kmem_cache *virtscsi_cmd_cache;
static mempool_t *virtscsi_cmd_pool;

static inline struct Scsi_Host *virtio_scsi_host(struct virtio_device *vdev)
{
	return vdev->priv;
}

static void virtscsi_compute_resid(struct scsi_cmnd *sc, u32 resid)
{
	if (!resid)
		return;

	if (!scsi_bidi_cmnd(sc)) {
		scsi_set_resid(sc, resid);
		return;
	}

	scsi_in(sc)->resid = min(resid, scsi_in(sc)->length);
	scsi_out(sc)->resid = resid - scsi_in(sc)->resid;
}

/**
 * virtscsi_complete_cmd - finish a scsi_cmd and invoke scsi_done
 *
 * Called with vq_lock held.
 */
static void virtscsi_complete_cmd(void *buf)
{
	struct virtio_scsi_cmd *cmd = buf;
	struct scsi_cmnd *sc = cmd->sc;
	struct virtio_scsi_cmd_resp *resp = &cmd->resp.cmd;

	dev_dbg(&sc->device->sdev_gendev,
		"cmd %p response %u status %#02x sense_len %u\n",
		sc, resp->response, resp->status, resp->sense_len);

	sc->result = resp->status;
	virtscsi_compute_resid(sc, resp->resid);
	switch (resp->response) {
	case VIRTIO_SCSI_S_OK:
		set_host_byte(sc, DID_OK);
		break;
	case VIRTIO_SCSI_S_OVERRUN:
		set_host_byte(sc, DID_ERROR);
		break;
	case VIRTIO_SCSI_S_ABORTED:
		set_host_byte(sc, DID_ABORT);
		break;
	case VIRTIO_SCSI_S_BAD_TARGET:
		set_host_byte(sc, DID_BAD_TARGET);
		break;
	case VIRTIO_SCSI_S_RESET:
		set_host_byte(sc, DID_RESET);
		break;
	case VIRTIO_SCSI_S_BUSY:
		set_host_byte(sc, DID_BUS_BUSY);
		break;
	case VIRTIO_SCSI_S_TRANSPORT_FAILURE:
		set_host_byte(sc, DID_TRANSPORT_DISRUPTED);
		break;
	case VIRTIO_SCSI_S_TARGET_FAILURE:
		set_host_byte(sc, DID_TARGET_FAILURE);
		break;
	case VIRTIO_SCSI_S_NEXUS_FAILURE:
		set_host_byte(sc, DID_NEXUS_FAILURE);
		break;
	default:
		scmd_printk(KERN_WARNING, sc, "Unknown response %d",
			    resp->response);
		/* fall through */
	case VIRTIO_SCSI_S_FAILURE:
		set_host_byte(sc, DID_ERROR);
		break;
	}

	WARN_ON(resp->sense_len > VIRTIO_SCSI_SENSE_SIZE);
	if (sc->sense_buffer) {
		memcpy(sc->sense_buffer, resp->sense,
		       min_t(u32, resp->sense_len, VIRTIO_SCSI_SENSE_SIZE));
		if (resp->sense_len)
			set_driver_byte(sc, DRIVER_SENSE);
	}

	mempool_free(cmd, virtscsi_cmd_pool);
	sc->scsi_done(sc);
}

static void virtscsi_vq_done(struct virtqueue *vq, void (*fn)(void *buf))
{
	void *buf;
	unsigned int len;

	do {
		virtqueue_disable_cb(vq);
		while ((buf = virtqueue_get_buf(vq, &len)) != NULL)
			fn(buf);
	} while (!virtqueue_enable_cb(vq));
}

static void virtscsi_req_done(struct virtqueue *vq)
{
	struct Scsi_Host *sh = virtio_scsi_host(vq->vdev);
	struct virtio_scsi *vscsi = shost_priv(sh);
	unsigned long flags;

	spin_lock_irqsave(&vscsi->req_vq.vq_lock, flags);
	virtscsi_vq_done(vq, virtscsi_complete_cmd);
	spin_unlock_irqrestore(&vscsi->req_vq.vq_lock, flags);
};

static void virtscsi_complete_free(void *buf)
{
	struct virtio_scsi_cmd *cmd = buf;

	if (cmd->comp)
		complete_all(cmd->comp);
	else
		mempool_free(cmd, virtscsi_cmd_pool);
}

static void virtscsi_ctrl_done(struct virtqueue *vq)
{
	struct Scsi_Host *sh = virtio_scsi_host(vq->vdev);
	struct virtio_scsi *vscsi = shost_priv(sh);
	unsigned long flags;

	spin_lock_irqsave(&vscsi->ctrl_vq.vq_lock, flags);
	virtscsi_vq_done(vq, virtscsi_complete_free);
	spin_unlock_irqrestore(&vscsi->ctrl_vq.vq_lock, flags);
};

static int virtscsi_kick_event(struct virtio_scsi *vscsi,
			       struct virtio_scsi_event_node *event_node)
{
	int ret;
	struct scatterlist sg;
	unsigned long flags;

	sg_set_buf(&sg, &event_node->event, sizeof(struct virtio_scsi_event));

	spin_lock_irqsave(&vscsi->event_vq.vq_lock, flags);

	ret = virtqueue_add_buf(vscsi->event_vq.vq, &sg, 0, 1, event_node, GFP_ATOMIC);
	if (ret >= 0)
		virtqueue_kick(vscsi->event_vq.vq);

	spin_unlock_irqrestore(&vscsi->event_vq.vq_lock, flags);

	return ret;
}

static int virtscsi_kick_event_all(struct virtio_scsi *vscsi)
{
	int i;

	for (i = 0; i < VIRTIO_SCSI_EVENT_LEN; i++) {
		vscsi->event_list[i].vscsi = vscsi;
		virtscsi_kick_event(vscsi, &vscsi->event_list[i]);
	}

	return 0;
}

static void virtscsi_cancel_event_work(struct virtio_scsi *vscsi)
{
	int i;

	for (i = 0; i < VIRTIO_SCSI_EVENT_LEN; i++)
		cancel_work_sync(&vscsi->event_list[i].work);
}

static void virtscsi_handle_transport_reset(struct virtio_scsi *vscsi,
						struct virtio_scsi_event *event)
{
	struct scsi_device *sdev;
	struct Scsi_Host *shost = virtio_scsi_host(vscsi->vdev);
	unsigned int target = event->lun[1];
	unsigned int lun = (event->lun[2] << 8) | event->lun[3];

	switch (event->reason) {
	case VIRTIO_SCSI_EVT_RESET_RESCAN:
		scsi_add_device(shost, 0, target, lun);
		break;
	case VIRTIO_SCSI_EVT_RESET_REMOVED:
		sdev = scsi_device_lookup(shost, 0, target, lun);
		if (sdev) {
			scsi_remove_device(sdev);
			scsi_device_put(sdev);
		} else {
			pr_err("SCSI device %d 0 %d %d not found\n",
				shost->host_no, target, lun);
		}
		break;
	default:
		pr_info("Unsupport virtio scsi event reason %x\n", event->reason);
	}
}

static void virtscsi_handle_event(struct work_struct *work)
{
	struct virtio_scsi_event_node *event_node =
		container_of(work, struct virtio_scsi_event_node, work);
	struct virtio_scsi *vscsi = event_node->vscsi;
	struct virtio_scsi_event *event = &event_node->event;

	if (event->event & VIRTIO_SCSI_T_EVENTS_MISSED) {
		event->event &= ~VIRTIO_SCSI_T_EVENTS_MISSED;
		scsi_scan_host(virtio_scsi_host(vscsi->vdev));
	}

	switch (event->event) {
	case VIRTIO_SCSI_T_NO_EVENT:
		break;
	case VIRTIO_SCSI_T_TRANSPORT_RESET:
		virtscsi_handle_transport_reset(vscsi, event);
		break;
	default:
		pr_err("Unsupport virtio scsi event %x\n", event->event);
	}
	virtscsi_kick_event(vscsi, event_node);
}

static void virtscsi_complete_event(void *buf)
{
	struct virtio_scsi_event_node *event_node = buf;

	INIT_WORK(&event_node->work, virtscsi_handle_event);
	schedule_work(&event_node->work);
}

static void virtscsi_event_done(struct virtqueue *vq)
{
	struct Scsi_Host *sh = virtio_scsi_host(vq->vdev);
	struct virtio_scsi *vscsi = shost_priv(sh);
	unsigned long flags;

	spin_lock_irqsave(&vscsi->event_vq.vq_lock, flags);
	virtscsi_vq_done(vq, virtscsi_complete_event);
	spin_unlock_irqrestore(&vscsi->event_vq.vq_lock, flags);
};

static void virtscsi_map_sgl(struct scatterlist *sg, unsigned int *p_idx,
			     struct scsi_data_buffer *sdb)
{
	struct sg_table *table = &sdb->table;
	struct scatterlist *sg_elem;
	unsigned int idx = *p_idx;
	int i;

	for_each_sg(table->sgl, sg_elem, table->nents, i)
		sg[idx++] = *sg_elem;

	*p_idx = idx;
}

/**
 * virtscsi_map_cmd - map a scsi_cmd to a virtqueue scatterlist
 * @vscsi	: virtio_scsi state
 * @cmd		: command structure
 * @out_num	: number of read-only elements
 * @in_num	: number of write-only elements
 * @req_size	: size of the request buffer
 * @resp_size	: size of the response buffer
 *
 * Called with tgt_lock held.
 */
static void virtscsi_map_cmd(struct virtio_scsi_target_state *tgt,
			     struct virtio_scsi_cmd *cmd,
			     unsigned *out_num, unsigned *in_num,
			     size_t req_size, size_t resp_size)
{
	struct scsi_cmnd *sc = cmd->sc;
	struct scatterlist *sg = tgt->sg;
	unsigned int idx = 0;

	/* Request header.  */
	sg_set_buf(&sg[idx++], &cmd->req, req_size);

	/* Data-out buffer.  */
	if (sc && sc->sc_data_direction != DMA_FROM_DEVICE)
		virtscsi_map_sgl(sg, &idx, scsi_out(sc));

	*out_num = idx;

	/* Response header.  */
	sg_set_buf(&sg[idx++], &cmd->resp, resp_size);

	/* Data-in buffer */
	if (sc && sc->sc_data_direction != DMA_TO_DEVICE)
		virtscsi_map_sgl(sg, &idx, scsi_in(sc));

	*in_num = idx - *out_num;
}

static int virtscsi_kick_cmd(struct virtio_scsi_target_state *tgt,
			     struct virtio_scsi_vq *vq,
			     struct virtio_scsi_cmd *cmd,
			     size_t req_size, size_t resp_size, gfp_t gfp)
{
	unsigned int out_num, in_num;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&tgt->tgt_lock, flags);
	virtscsi_map_cmd(tgt, cmd, &out_num, &in_num, req_size, resp_size);

	spin_lock(&vq->vq_lock);
	ret = virtqueue_add_buf(vq->vq, tgt->sg, out_num, in_num, cmd, gfp);
	spin_unlock(&tgt->tgt_lock);
	if (ret >= 0)
		ret = virtqueue_kick_prepare(vq->vq);

	spin_unlock_irqrestore(&vq->vq_lock, flags);

	if (ret > 0)
		virtqueue_notify(vq->vq);
	return ret;
}

static int virtscsi_queuecommand(struct Scsi_Host *sh, struct scsi_cmnd *sc)
{
	struct virtio_scsi *vscsi = shost_priv(sh);
	struct virtio_scsi_target_state *tgt = vscsi->tgt[sc->device->id];
	struct virtio_scsi_cmd *cmd;
	int ret;

	struct Scsi_Host *shost = virtio_scsi_host(vscsi->vdev);
	BUG_ON(scsi_sg_count(sc) > shost->sg_tablesize);

	/* TODO: check feature bit and fail if unsupported?  */
	BUG_ON(sc->sc_data_direction == DMA_BIDIRECTIONAL);

	dev_dbg(&sc->device->sdev_gendev,
		"cmd %p CDB: %#02x\n", sc, sc->cmnd[0]);

	ret = SCSI_MLQUEUE_HOST_BUSY;
	cmd = mempool_alloc(virtscsi_cmd_pool, GFP_ATOMIC);
	if (!cmd)
		goto out;

	memset(cmd, 0, sizeof(*cmd));
	cmd->sc = sc;
	cmd->req.cmd = (struct virtio_scsi_cmd_req){
		.lun[0] = 1,
		.lun[1] = sc->device->id,
		.lun[2] = (sc->device->lun >> 8) | 0x40,
		.lun[3] = sc->device->lun & 0xff,
		.tag = (unsigned long)sc,
		.task_attr = VIRTIO_SCSI_S_SIMPLE,
		.prio = 0,
		.crn = 0,
	};

	BUG_ON(sc->cmd_len > VIRTIO_SCSI_CDB_SIZE);
	memcpy(cmd->req.cmd.cdb, sc->cmnd, sc->cmd_len);

	if (virtscsi_kick_cmd(tgt, &vscsi->req_vq, cmd,
			      sizeof cmd->req.cmd, sizeof cmd->resp.cmd,
			      GFP_ATOMIC) >= 0)
		ret = 0;

out:
	return ret;
}

static int virtscsi_tmf(struct virtio_scsi *vscsi, struct virtio_scsi_cmd *cmd)
{
	DECLARE_COMPLETION_ONSTACK(comp);
	struct virtio_scsi_target_state *tgt = vscsi->tgt[cmd->sc->device->id];
	int ret = FAILED;

	cmd->comp = &comp;
	if (virtscsi_kick_cmd(tgt, &vscsi->ctrl_vq, cmd,
			      sizeof cmd->req.tmf, sizeof cmd->resp.tmf,
			      GFP_NOIO) < 0)
		goto out;

	wait_for_completion(&comp);
	if (cmd->resp.tmf.response == VIRTIO_SCSI_S_OK ||
	    cmd->resp.tmf.response == VIRTIO_SCSI_S_FUNCTION_SUCCEEDED)
		ret = SUCCESS;

out:
	mempool_free(cmd, virtscsi_cmd_pool);
	return ret;
}

static int virtscsi_device_reset(struct scsi_cmnd *sc)
{
	struct virtio_scsi *vscsi = shost_priv(sc->device->host);
	struct virtio_scsi_cmd *cmd;

	sdev_printk(KERN_INFO, sc->device, "device reset\n");
	cmd = mempool_alloc(virtscsi_cmd_pool, GFP_NOIO);
	if (!cmd)
		return FAILED;

	memset(cmd, 0, sizeof(*cmd));
	cmd->sc = sc;
	cmd->req.tmf = (struct virtio_scsi_ctrl_tmf_req){
		.type = VIRTIO_SCSI_T_TMF,
		.subtype = VIRTIO_SCSI_T_TMF_LOGICAL_UNIT_RESET,
		.lun[0] = 1,
		.lun[1] = sc->device->id,
		.lun[2] = (sc->device->lun >> 8) | 0x40,
		.lun[3] = sc->device->lun & 0xff,
	};
	return virtscsi_tmf(vscsi, cmd);
}

static int virtscsi_abort(struct scsi_cmnd *sc)
{
	struct virtio_scsi *vscsi = shost_priv(sc->device->host);
	struct virtio_scsi_cmd *cmd;

	scmd_printk(KERN_INFO, sc, "abort\n");
	cmd = mempool_alloc(virtscsi_cmd_pool, GFP_NOIO);
	if (!cmd)
		return FAILED;

	memset(cmd, 0, sizeof(*cmd));
	cmd->sc = sc;
	cmd->req.tmf = (struct virtio_scsi_ctrl_tmf_req){
		.type = VIRTIO_SCSI_T_TMF,
		.subtype = VIRTIO_SCSI_T_TMF_ABORT_TASK,
		.lun[0] = 1,
		.lun[1] = sc->device->id,
		.lun[2] = (sc->device->lun >> 8) | 0x40,
		.lun[3] = sc->device->lun & 0xff,
		.tag = (unsigned long)sc,
	};
	return virtscsi_tmf(vscsi, cmd);
}

static struct scsi_host_template virtscsi_host_template = {
	.module = THIS_MODULE,
	.name = "Virtio SCSI HBA",
	.proc_name = "virtio_scsi",
	.queuecommand = virtscsi_queuecommand,
	.this_id = -1,
	.eh_abort_handler = virtscsi_abort,
	.eh_device_reset_handler = virtscsi_device_reset,

	.can_queue = 1024,
	.dma_boundary = UINT_MAX,
	.use_clustering = ENABLE_CLUSTERING,
};

#define virtscsi_config_get(vdev, fld) \
	({ \
		typeof(((struct virtio_scsi_config *)0)->fld) __val; \
		vdev->config->get(vdev, \
				  offsetof(struct virtio_scsi_config, fld), \
				  &__val, sizeof(__val)); \
		__val; \
	})

#define virtscsi_config_set(vdev, fld, val) \
	(void)({ \
		typeof(((struct virtio_scsi_config *)0)->fld) __val = (val); \
		vdev->config->set(vdev, \
				  offsetof(struct virtio_scsi_config, fld), \
				  &__val, sizeof(__val)); \
	})

static void virtscsi_init_vq(struct virtio_scsi_vq *virtscsi_vq,
			     struct virtqueue *vq)
{
	spin_lock_init(&virtscsi_vq->vq_lock);
	virtscsi_vq->vq = vq;
}

static struct virtio_scsi_target_state *virtscsi_alloc_tgt(
	struct virtio_device *vdev, int sg_elems)
{
	struct virtio_scsi_target_state *tgt;
	gfp_t gfp_mask = GFP_KERNEL;

	/* We need extra sg elements at head and tail.  */
	tgt = kmalloc(sizeof(*tgt) + sizeof(tgt->sg[0]) * (sg_elems + 2),
		      gfp_mask);

	if (!tgt)
		return NULL;

	spin_lock_init(&tgt->tgt_lock);
	sg_init_table(tgt->sg, sg_elems + 2);
	return tgt;
}

static void virtscsi_scan(struct virtio_device *vdev)
{
	struct Scsi_Host *shost = (struct Scsi_Host *)vdev->priv;

	scsi_scan_host(shost);
}

static void virtscsi_remove_vqs(struct virtio_device *vdev)
{
	struct Scsi_Host *sh = virtio_scsi_host(vdev);
	struct virtio_scsi *vscsi = shost_priv(sh);
	u32 i, num_targets;

	/* Stop all the virtqueues. */
	vdev->config->reset(vdev);

	num_targets = sh->max_id;
	for (i = 0; i < num_targets; i++) {
		kfree(vscsi->tgt[i]);
		vscsi->tgt[i] = NULL;
	}

	vdev->config->del_vqs(vdev);
}

static int virtscsi_init(struct virtio_device *vdev,
			 struct virtio_scsi *vscsi, int num_targets)
{
	int err;
	struct virtqueue *vqs[3];
	u32 i, sg_elems;

	vq_callback_t *callbacks[] = {
		virtscsi_ctrl_done,
		virtscsi_event_done,
		virtscsi_req_done
	};
	const char *names[] = {
		"control",
		"event",
		"request"
	};

	/* Discover virtqueues and write information to configuration.  */
	err = vdev->config->find_vqs(vdev, 3, vqs, callbacks, names);
	if (err)
		return err;

	virtscsi_init_vq(&vscsi->ctrl_vq, vqs[0]);
	virtscsi_init_vq(&vscsi->event_vq, vqs[1]);
	virtscsi_init_vq(&vscsi->req_vq, vqs[2]);

	virtscsi_config_set(vdev, cdb_size, VIRTIO_SCSI_CDB_SIZE);
	virtscsi_config_set(vdev, sense_size, VIRTIO_SCSI_SENSE_SIZE);

	if (virtio_has_feature(vdev, VIRTIO_SCSI_F_HOTPLUG))
		virtscsi_kick_event_all(vscsi);

	/* We need to know how many segments before we allocate.  */
	sg_elems = virtscsi_config_get(vdev, seg_max) ?: 1;

	for (i = 0; i < num_targets; i++) {
		vscsi->tgt[i] = virtscsi_alloc_tgt(vdev, sg_elems);
		if (!vscsi->tgt[i]) {
			err = -ENOMEM;
			goto out;
		}
	}
	err = 0;

out:
	if (err)
		virtscsi_remove_vqs(vdev);
	return err;
}

static int __devinit virtscsi_probe(struct virtio_device *vdev)
{
	struct Scsi_Host *shost;
	struct virtio_scsi *vscsi;
	int err;
	u32 sg_elems, num_targets;
	u32 cmd_per_lun;

	/* Allocate memory and link the structs together.  */
	num_targets = virtscsi_config_get(vdev, max_target) + 1;
	shost = scsi_host_alloc(&virtscsi_host_template,
		sizeof(*vscsi)
		+ num_targets * sizeof(struct virtio_scsi_target_state));

	if (!shost)
		return -ENOMEM;

	sg_elems = virtscsi_config_get(vdev, seg_max) ?: 1;
	shost->sg_tablesize = sg_elems;
	vscsi = shost_priv(shost);
	vscsi->vdev = vdev;
	vdev->priv = shost;

	err = virtscsi_init(vdev, vscsi, num_targets);
	if (err)
		goto virtscsi_init_failed;

	cmd_per_lun = virtscsi_config_get(vdev, cmd_per_lun) ?: 1;
	shost->cmd_per_lun = min_t(u32, cmd_per_lun, shost->can_queue);
	shost->max_sectors = virtscsi_config_get(vdev, max_sectors) ?: 0xFFFF;
	shost->max_lun = virtscsi_config_get(vdev, max_lun) + 1;
	shost->max_id = num_targets;
	shost->max_channel = 0;
	shost->max_cmd_len = VIRTIO_SCSI_CDB_SIZE;
	err = scsi_add_host(shost, &vdev->dev);
	if (err)
		goto scsi_add_host_failed;
	/*
	 * scsi_scan_host() happens in virtscsi_scan() via virtio_driver->scan()
	 * after VIRTIO_CONFIG_S_DRIVER_OK has been set..
	 */
	return 0;

scsi_add_host_failed:
	vdev->config->del_vqs(vdev);
virtscsi_init_failed:
	scsi_host_put(shost);
	return err;
}

static void __devexit virtscsi_remove(struct virtio_device *vdev)
{
	struct Scsi_Host *shost = virtio_scsi_host(vdev);
	struct virtio_scsi *vscsi = shost_priv(shost);

	if (virtio_has_feature(vdev, VIRTIO_SCSI_F_HOTPLUG))
		virtscsi_cancel_event_work(vscsi);

	scsi_remove_host(shost);

	virtscsi_remove_vqs(vdev);
	scsi_host_put(shost);
}

#ifdef CONFIG_PM
static int virtscsi_freeze(struct virtio_device *vdev)
{
	virtscsi_remove_vqs(vdev);
	return 0;
}

static int virtscsi_restore(struct virtio_device *vdev)
{
	struct Scsi_Host *sh = virtio_scsi_host(vdev);
	struct virtio_scsi *vscsi = shost_priv(sh);

	return virtscsi_init(vdev, vscsi, sh->max_id);
}
#endif

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_SCSI, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_SCSI_F_HOTPLUG
};

static struct virtio_driver virtio_scsi_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtscsi_probe,
	.scan = virtscsi_scan,
#ifdef CONFIG_PM
	.freeze = virtscsi_freeze,
	.restore = virtscsi_restore,
#endif
	.remove = __devexit_p(virtscsi_remove),
};

static int __init init(void)
{
	int ret = -ENOMEM;

	virtscsi_cmd_cache = KMEM_CACHE(virtio_scsi_cmd, 0);
	if (!virtscsi_cmd_cache) {
		printk(KERN_ERR "kmem_cache_create() for "
				"virtscsi_cmd_cache failed\n");
		goto error;
	}


	virtscsi_cmd_pool =
		mempool_create_slab_pool(VIRTIO_SCSI_MEMPOOL_SZ,
					 virtscsi_cmd_cache);
	if (!virtscsi_cmd_pool) {
		printk(KERN_ERR "mempool_create() for"
				"virtscsi_cmd_pool failed\n");
		goto error;
	}
	ret = register_virtio_driver(&virtio_scsi_driver);
	if (ret < 0)
		goto error;

	return 0;

error:
	if (virtscsi_cmd_pool) {
		mempool_destroy(virtscsi_cmd_pool);
		virtscsi_cmd_pool = NULL;
	}
	if (virtscsi_cmd_cache) {
		kmem_cache_destroy(virtscsi_cmd_cache);
		virtscsi_cmd_cache = NULL;
	}
	return ret;
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_scsi_driver);
	mempool_destroy(virtscsi_cmd_pool);
	kmem_cache_destroy(virtscsi_cmd_cache);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio SCSI HBA driver");
MODULE_LICENSE("GPL");
