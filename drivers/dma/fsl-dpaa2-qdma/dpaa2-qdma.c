// SPDX-License-Identifier: GPL-2.0
// Copyright 2019 NXP

#include <linux/init.h>
#include <linux/module.h>
#include <linux/dmapool.h>
#include <linux/of_irq.h>
#include <linux/iommu.h>
#include <linux/sys_soc.h>
#include <linux/fsl/mc.h>
#include <soc/fsl/dpaa2-io.h>

#include "../virt-dma.h"
#include "dpdmai.h"
#include "dpaa2-qdma.h"

static bool smmu_disable = true;

static struct dpaa2_qdma_chan *to_dpaa2_qdma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct dpaa2_qdma_chan, vchan.chan);
}

static struct dpaa2_qdma_comp *to_fsl_qdma_comp(struct virt_dma_desc *vd)
{
	return container_of(vd, struct dpaa2_qdma_comp, vdesc);
}

static int dpaa2_qdma_alloc_chan_resources(struct dma_chan *chan)
{
	struct dpaa2_qdma_chan *dpaa2_chan = to_dpaa2_qdma_chan(chan);
	struct dpaa2_qdma_engine *dpaa2_qdma = dpaa2_chan->qdma;
	struct device *dev = &dpaa2_qdma->priv->dpdmai_dev->dev;

	dpaa2_chan->fd_pool = dma_pool_create("fd_pool", dev,
					      sizeof(struct dpaa2_fd),
					      sizeof(struct dpaa2_fd), 0);
	if (!dpaa2_chan->fd_pool)
		goto err;

	dpaa2_chan->fl_pool = dma_pool_create("fl_pool", dev,
					      sizeof(struct dpaa2_fl_entry),
					      sizeof(struct dpaa2_fl_entry), 0);
	if (!dpaa2_chan->fl_pool)
		goto err_fd;

	dpaa2_chan->sdd_pool =
		dma_pool_create("sdd_pool", dev,
				sizeof(struct dpaa2_qdma_sd_d),
				sizeof(struct dpaa2_qdma_sd_d), 0);
	if (!dpaa2_chan->sdd_pool)
		goto err_fl;

	return dpaa2_qdma->desc_allocated++;
err_fl:
	dma_pool_destroy(dpaa2_chan->fl_pool);
err_fd:
	dma_pool_destroy(dpaa2_chan->fd_pool);
err:
	return -ENOMEM;
}

static void dpaa2_qdma_free_chan_resources(struct dma_chan *chan)
{
	struct dpaa2_qdma_chan *dpaa2_chan = to_dpaa2_qdma_chan(chan);
	struct dpaa2_qdma_engine *dpaa2_qdma = dpaa2_chan->qdma;
	unsigned long flags;

	LIST_HEAD(head);

	spin_lock_irqsave(&dpaa2_chan->vchan.lock, flags);
	vchan_get_all_descriptors(&dpaa2_chan->vchan, &head);
	spin_unlock_irqrestore(&dpaa2_chan->vchan.lock, flags);

	vchan_dma_desc_free_list(&dpaa2_chan->vchan, &head);

	dpaa2_dpdmai_free_comp(dpaa2_chan, &dpaa2_chan->comp_used);
	dpaa2_dpdmai_free_comp(dpaa2_chan, &dpaa2_chan->comp_free);

	dma_pool_destroy(dpaa2_chan->fd_pool);
	dma_pool_destroy(dpaa2_chan->fl_pool);
	dma_pool_destroy(dpaa2_chan->sdd_pool);
	dpaa2_qdma->desc_allocated--;
}

/*
 * Request a command descriptor for enqueue.
 */
static struct dpaa2_qdma_comp *
dpaa2_qdma_request_desc(struct dpaa2_qdma_chan *dpaa2_chan)
{
	struct dpaa2_qdma_priv *qdma_priv = dpaa2_chan->qdma->priv;
	struct device *dev = &qdma_priv->dpdmai_dev->dev;
	struct dpaa2_qdma_comp *comp_temp = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dpaa2_chan->queue_lock, flags);
	if (list_empty(&dpaa2_chan->comp_free)) {
		spin_unlock_irqrestore(&dpaa2_chan->queue_lock, flags);
		comp_temp = kzalloc(sizeof(*comp_temp), GFP_NOWAIT);
		if (!comp_temp)
			goto err;
		comp_temp->fd_virt_addr =
			dma_pool_alloc(dpaa2_chan->fd_pool, GFP_NOWAIT,
				       &comp_temp->fd_bus_addr);
		if (!comp_temp->fd_virt_addr)
			goto err_comp;

		comp_temp->fl_virt_addr =
			dma_pool_alloc(dpaa2_chan->fl_pool, GFP_NOWAIT,
				       &comp_temp->fl_bus_addr);
		if (!comp_temp->fl_virt_addr)
			goto err_fd_virt;

		comp_temp->desc_virt_addr =
			dma_pool_alloc(dpaa2_chan->sdd_pool, GFP_NOWAIT,
				       &comp_temp->desc_bus_addr);
		if (!comp_temp->desc_virt_addr)
			goto err_fl_virt;

		comp_temp->qchan = dpaa2_chan;
		return comp_temp;
	}

	comp_temp = list_first_entry(&dpaa2_chan->comp_free,
				     struct dpaa2_qdma_comp, list);
	list_del(&comp_temp->list);
	spin_unlock_irqrestore(&dpaa2_chan->queue_lock, flags);

	comp_temp->qchan = dpaa2_chan;

	return comp_temp;

err_fl_virt:
		dma_pool_free(dpaa2_chan->fl_pool,
			      comp_temp->fl_virt_addr,
			      comp_temp->fl_bus_addr);
err_fd_virt:
		dma_pool_free(dpaa2_chan->fd_pool,
			      comp_temp->fd_virt_addr,
			      comp_temp->fd_bus_addr);
err_comp:
	kfree(comp_temp);
err:
	dev_err(dev, "Failed to request descriptor\n");
	return NULL;
}

static void
dpaa2_qdma_populate_fd(u32 format, struct dpaa2_qdma_comp *dpaa2_comp)
{
	struct dpaa2_fd *fd;

	fd = dpaa2_comp->fd_virt_addr;
	memset(fd, 0, sizeof(struct dpaa2_fd));

	/* fd populated */
	dpaa2_fd_set_addr(fd, dpaa2_comp->fl_bus_addr);

	/*
	 * Bypass memory translation, Frame list format, short length disable
	 * we need to disable BMT if fsl-mc use iova addr
	 */
	if (smmu_disable)
		dpaa2_fd_set_bpid(fd, QMAN_FD_BMT_ENABLE);
	dpaa2_fd_set_format(fd, QMAN_FD_FMT_ENABLE | QMAN_FD_SL_DISABLE);

	dpaa2_fd_set_frc(fd, format | QDMA_SER_CTX);
}

/* first frame list for descriptor buffer */
static void
dpaa2_qdma_populate_first_framel(struct dpaa2_fl_entry *f_list,
				 struct dpaa2_qdma_comp *dpaa2_comp,
				 bool wrt_changed)
{
	struct dpaa2_qdma_sd_d *sdd;

	sdd = dpaa2_comp->desc_virt_addr;
	memset(sdd, 0, 2 * (sizeof(*sdd)));

	/* source descriptor CMD */
	sdd->cmd = cpu_to_le32(QDMA_SD_CMD_RDTTYPE_COHERENT);
	sdd++;

	/* dest descriptor CMD */
	if (wrt_changed)
		sdd->cmd = cpu_to_le32(LX2160_QDMA_DD_CMD_WRTTYPE_COHERENT);
	else
		sdd->cmd = cpu_to_le32(QDMA_DD_CMD_WRTTYPE_COHERENT);

	memset(f_list, 0, sizeof(struct dpaa2_fl_entry));

	/* first frame list to source descriptor */
	dpaa2_fl_set_addr(f_list, dpaa2_comp->desc_bus_addr);
	dpaa2_fl_set_len(f_list, 0x20);
	dpaa2_fl_set_format(f_list, QDMA_FL_FMT_SBF | QDMA_FL_SL_LONG);

	/* bypass memory translation */
	if (smmu_disable)
		f_list->bpid = cpu_to_le16(QDMA_FL_BMT_ENABLE);
}

/* source and destination frame list */
static void
dpaa2_qdma_populate_frames(struct dpaa2_fl_entry *f_list,
			   dma_addr_t dst, dma_addr_t src,
			   size_t len, uint8_t fmt)
{
	/* source frame list to source buffer */
	memset(f_list, 0, sizeof(struct dpaa2_fl_entry));

	dpaa2_fl_set_addr(f_list, src);
	dpaa2_fl_set_len(f_list, len);

	/* single buffer frame or scatter gather frame */
	dpaa2_fl_set_format(f_list, (fmt | QDMA_FL_SL_LONG));

	/* bypass memory translation */
	if (smmu_disable)
		f_list->bpid = cpu_to_le16(QDMA_FL_BMT_ENABLE);

	f_list++;

	/* destination frame list to destination buffer */
	memset(f_list, 0, sizeof(struct dpaa2_fl_entry));

	dpaa2_fl_set_addr(f_list, dst);
	dpaa2_fl_set_len(f_list, len);
	dpaa2_fl_set_format(f_list, (fmt | QDMA_FL_SL_LONG));
	/* single buffer frame or scatter gather frame */
	dpaa2_fl_set_final(f_list, QDMA_FL_F);
	/* bypass memory translation */
	if (smmu_disable)
		f_list->bpid = cpu_to_le16(QDMA_FL_BMT_ENABLE);
}

static struct dma_async_tx_descriptor
*dpaa2_qdma_prep_memcpy(struct dma_chan *chan, dma_addr_t dst,
			dma_addr_t src, size_t len, ulong flags)
{
	struct dpaa2_qdma_chan *dpaa2_chan = to_dpaa2_qdma_chan(chan);
	struct dpaa2_qdma_engine *dpaa2_qdma;
	struct dpaa2_qdma_comp *dpaa2_comp;
	struct dpaa2_fl_entry *f_list;
	bool wrt_changed;

	dpaa2_qdma = dpaa2_chan->qdma;
	dpaa2_comp = dpaa2_qdma_request_desc(dpaa2_chan);
	if (!dpaa2_comp)
		return NULL;

	wrt_changed = (bool)dpaa2_qdma->qdma_wrtype_fixup;

	/* populate Frame descriptor */
	dpaa2_qdma_populate_fd(QDMA_FD_LONG_FORMAT, dpaa2_comp);

	f_list = dpaa2_comp->fl_virt_addr;

	/* first frame list for descriptor buffer (logn format) */
	dpaa2_qdma_populate_first_framel(f_list, dpaa2_comp, wrt_changed);

	f_list++;

	dpaa2_qdma_populate_frames(f_list, dst, src, len, QDMA_FL_FMT_SBF);

	return vchan_tx_prep(&dpaa2_chan->vchan, &dpaa2_comp->vdesc, flags);
}

static void dpaa2_qdma_issue_pending(struct dma_chan *chan)
{
	struct dpaa2_qdma_chan *dpaa2_chan = to_dpaa2_qdma_chan(chan);
	struct dpaa2_qdma_comp *dpaa2_comp;
	struct virt_dma_desc *vdesc;
	struct dpaa2_fd *fd;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&dpaa2_chan->queue_lock, flags);
	spin_lock(&dpaa2_chan->vchan.lock);
	if (vchan_issue_pending(&dpaa2_chan->vchan)) {
		vdesc = vchan_next_desc(&dpaa2_chan->vchan);
		if (!vdesc)
			goto err_enqueue;
		dpaa2_comp = to_fsl_qdma_comp(vdesc);

		fd = dpaa2_comp->fd_virt_addr;

		list_del(&vdesc->node);
		list_add_tail(&dpaa2_comp->list, &dpaa2_chan->comp_used);

		err = dpaa2_io_service_enqueue_fq(NULL, dpaa2_chan->fqid, fd);
		if (err) {
			list_del(&dpaa2_comp->list);
			list_add_tail(&dpaa2_comp->list,
				      &dpaa2_chan->comp_free);
		}
	}
err_enqueue:
	spin_unlock(&dpaa2_chan->vchan.lock);
	spin_unlock_irqrestore(&dpaa2_chan->queue_lock, flags);
}

static int __cold dpaa2_qdma_setup(struct fsl_mc_device *ls_dev)
{
	struct dpaa2_qdma_priv_per_prio *ppriv;
	struct device *dev = &ls_dev->dev;
	struct dpaa2_qdma_priv *priv;
	u8 prio_def = DPDMAI_PRIO_NUM;
	int err = -EINVAL;
	int i;

	priv = dev_get_drvdata(dev);

	priv->dev = dev;
	priv->dpqdma_id = ls_dev->obj_desc.id;

	/* Get the handle for the DPDMAI this interface is associate with */
	err = dpdmai_open(priv->mc_io, 0, priv->dpqdma_id, &ls_dev->mc_handle);
	if (err) {
		dev_err(dev, "dpdmai_open() failed\n");
		return err;
	}

	dev_dbg(dev, "Opened dpdmai object successfully\n");

	err = dpdmai_get_attributes(priv->mc_io, 0, ls_dev->mc_handle,
				    &priv->dpdmai_attr);
	if (err) {
		dev_err(dev, "dpdmai_get_attributes() failed\n");
		goto exit;
	}

	if (priv->dpdmai_attr.version.major > DPDMAI_VER_MAJOR) {
		dev_err(dev, "DPDMAI major version mismatch\n"
			     "Found %u.%u, supported version is %u.%u\n",
				priv->dpdmai_attr.version.major,
				priv->dpdmai_attr.version.minor,
				DPDMAI_VER_MAJOR, DPDMAI_VER_MINOR);
		goto exit;
	}

	if (priv->dpdmai_attr.version.minor > DPDMAI_VER_MINOR) {
		dev_err(dev, "DPDMAI minor version mismatch\n"
			     "Found %u.%u, supported version is %u.%u\n",
				priv->dpdmai_attr.version.major,
				priv->dpdmai_attr.version.minor,
				DPDMAI_VER_MAJOR, DPDMAI_VER_MINOR);
		goto exit;
	}

	priv->num_pairs = min(priv->dpdmai_attr.num_of_priorities, prio_def);
	ppriv = kcalloc(priv->num_pairs, sizeof(*ppriv), GFP_KERNEL);
	if (!ppriv) {
		err = -ENOMEM;
		goto exit;
	}
	priv->ppriv = ppriv;

	for (i = 0; i < priv->num_pairs; i++) {
		err = dpdmai_get_rx_queue(priv->mc_io, 0, ls_dev->mc_handle,
					  i, &priv->rx_queue_attr[i]);
		if (err) {
			dev_err(dev, "dpdmai_get_rx_queue() failed\n");
			goto exit;
		}
		ppriv->rsp_fqid = priv->rx_queue_attr[i].fqid;

		err = dpdmai_get_tx_queue(priv->mc_io, 0, ls_dev->mc_handle,
					  i, &priv->tx_fqid[i]);
		if (err) {
			dev_err(dev, "dpdmai_get_tx_queue() failed\n");
			goto exit;
		}
		ppriv->req_fqid = priv->tx_fqid[i];
		ppriv->prio = i;
		ppriv->priv = priv;
		ppriv++;
	}

	return 0;
exit:
	dpdmai_close(priv->mc_io, 0, ls_dev->mc_handle);
	return err;
}

static void dpaa2_qdma_fqdan_cb(struct dpaa2_io_notification_ctx *ctx)
{
	struct dpaa2_qdma_priv_per_prio *ppriv = container_of(ctx,
			struct dpaa2_qdma_priv_per_prio, nctx);
	struct dpaa2_qdma_comp *dpaa2_comp, *_comp_tmp;
	struct dpaa2_qdma_priv *priv = ppriv->priv;
	u32 n_chans = priv->dpaa2_qdma->n_chans;
	struct dpaa2_qdma_chan *qchan;
	const struct dpaa2_fd *fd_eq;
	const struct dpaa2_fd *fd;
	struct dpaa2_dq *dq;
	int is_last = 0;
	int found;
	u8 status;
	int err;
	int i;

	do {
		err = dpaa2_io_service_pull_fq(NULL, ppriv->rsp_fqid,
					       ppriv->store);
	} while (err);

	while (!is_last) {
		do {
			dq = dpaa2_io_store_next(ppriv->store, &is_last);
		} while (!is_last && !dq);
		if (!dq) {
			dev_err(priv->dev, "FQID returned no valid frames!\n");
			continue;
		}

		/* obtain FD and process the error */
		fd = dpaa2_dq_fd(dq);

		status = dpaa2_fd_get_ctrl(fd) & 0xff;
		if (status)
			dev_err(priv->dev, "FD error occurred\n");
		found = 0;
		for (i = 0; i < n_chans; i++) {
			qchan = &priv->dpaa2_qdma->chans[i];
			spin_lock(&qchan->queue_lock);
			if (list_empty(&qchan->comp_used)) {
				spin_unlock(&qchan->queue_lock);
				continue;
			}
			list_for_each_entry_safe(dpaa2_comp, _comp_tmp,
						 &qchan->comp_used, list) {
				fd_eq = dpaa2_comp->fd_virt_addr;

				if (le64_to_cpu(fd_eq->simple.addr) ==
				    le64_to_cpu(fd->simple.addr)) {
					spin_lock(&qchan->vchan.lock);
					vchan_cookie_complete(&
							dpaa2_comp->vdesc);
					spin_unlock(&qchan->vchan.lock);
					found = 1;
					break;
				}
			}
			spin_unlock(&qchan->queue_lock);
			if (found)
				break;
		}
	}

	dpaa2_io_service_rearm(NULL, ctx);
}

static int __cold dpaa2_qdma_dpio_setup(struct dpaa2_qdma_priv *priv)
{
	struct dpaa2_qdma_priv_per_prio *ppriv;
	struct device *dev = priv->dev;
	int err = -EINVAL;
	int i, num;

	num = priv->num_pairs;
	ppriv = priv->ppriv;
	for (i = 0; i < num; i++) {
		ppriv->nctx.is_cdan = 0;
		ppriv->nctx.desired_cpu = DPAA2_IO_ANY_CPU;
		ppriv->nctx.id = ppriv->rsp_fqid;
		ppriv->nctx.cb = dpaa2_qdma_fqdan_cb;
		err = dpaa2_io_service_register(NULL, &ppriv->nctx, dev);
		if (err) {
			dev_err(dev, "Notification register failed\n");
			goto err_service;
		}

		ppriv->store =
			dpaa2_io_store_create(DPAA2_QDMA_STORE_SIZE, dev);
		if (!ppriv->store) {
			dev_err(dev, "dpaa2_io_store_create() failed\n");
			goto err_store;
		}

		ppriv++;
	}
	return 0;

err_store:
	dpaa2_io_service_deregister(NULL, &ppriv->nctx, dev);
err_service:
	ppriv--;
	while (ppriv >= priv->ppriv) {
		dpaa2_io_service_deregister(NULL, &ppriv->nctx, dev);
		dpaa2_io_store_destroy(ppriv->store);
		ppriv--;
	}
	return err;
}

static void dpaa2_dpmai_store_free(struct dpaa2_qdma_priv *priv)
{
	struct dpaa2_qdma_priv_per_prio *ppriv = priv->ppriv;
	int i;

	for (i = 0; i < priv->num_pairs; i++) {
		dpaa2_io_store_destroy(ppriv->store);
		ppriv++;
	}
}

static void dpaa2_dpdmai_dpio_free(struct dpaa2_qdma_priv *priv)
{
	struct dpaa2_qdma_priv_per_prio *ppriv = priv->ppriv;
	struct device *dev = priv->dev;
	int i;

	for (i = 0; i < priv->num_pairs; i++) {
		dpaa2_io_service_deregister(NULL, &ppriv->nctx, dev);
		ppriv++;
	}
}

static int __cold dpaa2_dpdmai_bind(struct dpaa2_qdma_priv *priv)
{
	struct dpdmai_rx_queue_cfg rx_queue_cfg;
	struct dpaa2_qdma_priv_per_prio *ppriv;
	struct device *dev = priv->dev;
	struct fsl_mc_device *ls_dev;
	int i, num;
	int err;

	ls_dev = to_fsl_mc_device(dev);
	num = priv->num_pairs;
	ppriv = priv->ppriv;
	for (i = 0; i < num; i++) {
		rx_queue_cfg.options = DPDMAI_QUEUE_OPT_USER_CTX |
					DPDMAI_QUEUE_OPT_DEST;
		rx_queue_cfg.user_ctx = ppriv->nctx.qman64;
		rx_queue_cfg.dest_cfg.dest_type = DPDMAI_DEST_DPIO;
		rx_queue_cfg.dest_cfg.dest_id = ppriv->nctx.dpio_id;
		rx_queue_cfg.dest_cfg.priority = ppriv->prio;
		err = dpdmai_set_rx_queue(priv->mc_io, 0, ls_dev->mc_handle,
					  rx_queue_cfg.dest_cfg.priority,
					  &rx_queue_cfg);
		if (err) {
			dev_err(dev, "dpdmai_set_rx_queue() failed\n");
			return err;
		}

		ppriv++;
	}

	return 0;
}

static int __cold dpaa2_dpdmai_dpio_unbind(struct dpaa2_qdma_priv *priv)
{
	struct dpaa2_qdma_priv_per_prio *ppriv = priv->ppriv;
	struct device *dev = priv->dev;
	struct fsl_mc_device *ls_dev;
	int err = 0;
	int i;

	ls_dev = to_fsl_mc_device(dev);

	for (i = 0; i < priv->num_pairs; i++) {
		ppriv->nctx.qman64 = 0;
		ppriv->nctx.dpio_id = 0;
		ppriv++;
	}

	err = dpdmai_reset(priv->mc_io, 0, ls_dev->mc_handle);
	if (err)
		dev_err(dev, "dpdmai_reset() failed\n");

	return err;
}

static void dpaa2_dpdmai_free_comp(struct dpaa2_qdma_chan *qchan,
				   struct list_head *head)
{
	struct dpaa2_qdma_comp *comp_tmp, *_comp_tmp;
	unsigned long flags;

	list_for_each_entry_safe(comp_tmp, _comp_tmp,
				 head, list) {
		spin_lock_irqsave(&qchan->queue_lock, flags);
		list_del(&comp_tmp->list);
		spin_unlock_irqrestore(&qchan->queue_lock, flags);
		dma_pool_free(qchan->fd_pool,
			      comp_tmp->fd_virt_addr,
			      comp_tmp->fd_bus_addr);
		dma_pool_free(qchan->fl_pool,
			      comp_tmp->fl_virt_addr,
			      comp_tmp->fl_bus_addr);
		dma_pool_free(qchan->sdd_pool,
			      comp_tmp->desc_virt_addr,
			      comp_tmp->desc_bus_addr);
		kfree(comp_tmp);
	}
}

static void dpaa2_dpdmai_free_channels(struct dpaa2_qdma_engine *dpaa2_qdma)
{
	struct dpaa2_qdma_chan *qchan;
	int num, i;

	num = dpaa2_qdma->n_chans;
	for (i = 0; i < num; i++) {
		qchan = &dpaa2_qdma->chans[i];
		dpaa2_dpdmai_free_comp(qchan, &qchan->comp_used);
		dpaa2_dpdmai_free_comp(qchan, &qchan->comp_free);
		dma_pool_destroy(qchan->fd_pool);
		dma_pool_destroy(qchan->fl_pool);
		dma_pool_destroy(qchan->sdd_pool);
	}
}

static void dpaa2_qdma_free_desc(struct virt_dma_desc *vdesc)
{
	struct dpaa2_qdma_comp *dpaa2_comp;
	struct dpaa2_qdma_chan *qchan;
	unsigned long flags;

	dpaa2_comp = to_fsl_qdma_comp(vdesc);
	qchan = dpaa2_comp->qchan;
	spin_lock_irqsave(&qchan->queue_lock, flags);
	list_del(&dpaa2_comp->list);
	list_add_tail(&dpaa2_comp->list, &qchan->comp_free);
	spin_unlock_irqrestore(&qchan->queue_lock, flags);
}

static int dpaa2_dpdmai_init_channels(struct dpaa2_qdma_engine *dpaa2_qdma)
{
	struct dpaa2_qdma_priv *priv = dpaa2_qdma->priv;
	struct dpaa2_qdma_chan *dpaa2_chan;
	int num = priv->num_pairs;
	int i;

	INIT_LIST_HEAD(&dpaa2_qdma->dma_dev.channels);
	for (i = 0; i < dpaa2_qdma->n_chans; i++) {
		dpaa2_chan = &dpaa2_qdma->chans[i];
		dpaa2_chan->qdma = dpaa2_qdma;
		dpaa2_chan->fqid = priv->tx_fqid[i % num];
		dpaa2_chan->vchan.desc_free = dpaa2_qdma_free_desc;
		vchan_init(&dpaa2_chan->vchan, &dpaa2_qdma->dma_dev);
		spin_lock_init(&dpaa2_chan->queue_lock);
		INIT_LIST_HEAD(&dpaa2_chan->comp_used);
		INIT_LIST_HEAD(&dpaa2_chan->comp_free);
	}
	return 0;
}

static int dpaa2_qdma_probe(struct fsl_mc_device *dpdmai_dev)
{
	struct device *dev = &dpdmai_dev->dev;
	struct dpaa2_qdma_engine *dpaa2_qdma;
	struct dpaa2_qdma_priv *priv;
	int err;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_set_drvdata(dev, priv);
	priv->dpdmai_dev = dpdmai_dev;

	priv->iommu_domain = iommu_get_domain_for_dev(dev);
	if (priv->iommu_domain)
		smmu_disable = false;

	/* obtain a MC portal */
	err = fsl_mc_portal_allocate(dpdmai_dev, 0, &priv->mc_io);
	if (err) {
		if (err == -ENXIO)
			err = -EPROBE_DEFER;
		else
			dev_err(dev, "MC portal allocation failed\n");
		goto err_mcportal;
	}

	/* DPDMAI initialization */
	err = dpaa2_qdma_setup(dpdmai_dev);
	if (err) {
		dev_err(dev, "dpaa2_dpdmai_setup() failed\n");
		goto err_dpdmai_setup;
	}

	/* DPIO */
	err = dpaa2_qdma_dpio_setup(priv);
	if (err) {
		dev_err(dev, "dpaa2_dpdmai_dpio_setup() failed\n");
		goto err_dpio_setup;
	}

	/* DPDMAI binding to DPIO */
	err = dpaa2_dpdmai_bind(priv);
	if (err) {
		dev_err(dev, "dpaa2_dpdmai_bind() failed\n");
		goto err_bind;
	}

	/* DPDMAI enable */
	err = dpdmai_enable(priv->mc_io, 0, dpdmai_dev->mc_handle);
	if (err) {
		dev_err(dev, "dpdmai_enable() faile\n");
		goto err_enable;
	}

	dpaa2_qdma = kzalloc(sizeof(*dpaa2_qdma), GFP_KERNEL);
	if (!dpaa2_qdma) {
		err = -ENOMEM;
		goto err_eng;
	}

	priv->dpaa2_qdma = dpaa2_qdma;
	dpaa2_qdma->priv = priv;

	dpaa2_qdma->desc_allocated = 0;
	dpaa2_qdma->n_chans = NUM_CH;

	dpaa2_dpdmai_init_channels(dpaa2_qdma);

	if (soc_device_match(soc_fixup_tuning))
		dpaa2_qdma->qdma_wrtype_fixup = true;
	else
		dpaa2_qdma->qdma_wrtype_fixup = false;

	dma_cap_set(DMA_PRIVATE, dpaa2_qdma->dma_dev.cap_mask);
	dma_cap_set(DMA_SLAVE, dpaa2_qdma->dma_dev.cap_mask);
	dma_cap_set(DMA_MEMCPY, dpaa2_qdma->dma_dev.cap_mask);

	dpaa2_qdma->dma_dev.dev = dev;
	dpaa2_qdma->dma_dev.device_alloc_chan_resources =
		dpaa2_qdma_alloc_chan_resources;
	dpaa2_qdma->dma_dev.device_free_chan_resources =
		dpaa2_qdma_free_chan_resources;
	dpaa2_qdma->dma_dev.device_tx_status = dma_cookie_status;
	dpaa2_qdma->dma_dev.device_prep_dma_memcpy = dpaa2_qdma_prep_memcpy;
	dpaa2_qdma->dma_dev.device_issue_pending = dpaa2_qdma_issue_pending;

	err = dma_async_device_register(&dpaa2_qdma->dma_dev);
	if (err) {
		dev_err(dev, "Can't register NXP QDMA engine.\n");
		goto err_dpaa2_qdma;
	}

	return 0;

err_dpaa2_qdma:
	kfree(dpaa2_qdma);
err_eng:
	dpdmai_disable(priv->mc_io, 0, dpdmai_dev->mc_handle);
err_enable:
	dpaa2_dpdmai_dpio_unbind(priv);
err_bind:
	dpaa2_dpmai_store_free(priv);
	dpaa2_dpdmai_dpio_free(priv);
err_dpio_setup:
	kfree(priv->ppriv);
	dpdmai_close(priv->mc_io, 0, dpdmai_dev->mc_handle);
err_dpdmai_setup:
	fsl_mc_portal_free(priv->mc_io);
err_mcportal:
	kfree(priv);
	dev_set_drvdata(dev, NULL);
	return err;
}

static int dpaa2_qdma_remove(struct fsl_mc_device *ls_dev)
{
	struct dpaa2_qdma_engine *dpaa2_qdma;
	struct dpaa2_qdma_priv *priv;
	struct device *dev;

	dev = &ls_dev->dev;
	priv = dev_get_drvdata(dev);
	dpaa2_qdma = priv->dpaa2_qdma;

	dpdmai_disable(priv->mc_io, 0, ls_dev->mc_handle);
	dpaa2_dpdmai_dpio_unbind(priv);
	dpaa2_dpmai_store_free(priv);
	dpaa2_dpdmai_dpio_free(priv);
	dpdmai_close(priv->mc_io, 0, ls_dev->mc_handle);
	fsl_mc_portal_free(priv->mc_io);
	dev_set_drvdata(dev, NULL);
	dpaa2_dpdmai_free_channels(dpaa2_qdma);

	dma_async_device_unregister(&dpaa2_qdma->dma_dev);
	kfree(priv);
	kfree(dpaa2_qdma);

	return 0;
}

static const struct fsl_mc_device_id dpaa2_qdma_id_table[] = {
	{
		.vendor = FSL_MC_VENDOR_FREESCALE,
		.obj_type = "dpdmai",
	},
	{ .vendor = 0x0 }
};

static struct fsl_mc_driver dpaa2_qdma_driver = {
	.driver		= {
		.name	= "dpaa2-qdma",
		.owner  = THIS_MODULE,
	},
	.probe          = dpaa2_qdma_probe,
	.remove		= dpaa2_qdma_remove,
	.match_id_table	= dpaa2_qdma_id_table
};

static int __init dpaa2_qdma_driver_init(void)
{
	return fsl_mc_driver_register(&(dpaa2_qdma_driver));
}
late_initcall(dpaa2_qdma_driver_init);

static void __exit fsl_qdma_exit(void)
{
	fsl_mc_driver_unregister(&(dpaa2_qdma_driver));
}
module_exit(fsl_qdma_exit);

MODULE_ALIAS("platform:fsl-dpaa2-qdma");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NXP Layerscape DPAA2 qDMA engine driver");
