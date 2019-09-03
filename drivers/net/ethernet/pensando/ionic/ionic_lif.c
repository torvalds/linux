// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/cpumask.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_debugfs.h"

static irqreturn_t ionic_isr(int irq, void *data)
{
	struct napi_struct *napi = data;

	napi_schedule_irqoff(napi);

	return IRQ_HANDLED;
}

static int ionic_request_irq(struct ionic_lif *lif, struct ionic_qcq *qcq)
{
	struct ionic_intr_info *intr = &qcq->intr;
	struct device *dev = lif->ionic->dev;
	struct ionic_queue *q = &qcq->q;
	const char *name;

	if (lif->registered)
		name = lif->netdev->name;
	else
		name = dev_name(dev);

	snprintf(intr->name, sizeof(intr->name),
		 "%s-%s-%s", IONIC_DRV_NAME, name, q->name);

	return devm_request_irq(dev, intr->vector, ionic_isr,
				0, intr->name, &qcq->napi);
}

static int ionic_intr_alloc(struct ionic_lif *lif, struct ionic_intr_info *intr)
{
	struct ionic *ionic = lif->ionic;
	int index;

	index = find_first_zero_bit(ionic->intrs, ionic->nintrs);
	if (index == ionic->nintrs) {
		netdev_warn(lif->netdev, "%s: no intr, index=%d nintrs=%d\n",
			    __func__, index, ionic->nintrs);
		return -ENOSPC;
	}

	set_bit(index, ionic->intrs);
	ionic_intr_init(&ionic->idev, intr, index);

	return 0;
}

static void ionic_intr_free(struct ionic_lif *lif, int index)
{
	if (index != INTR_INDEX_NOT_ASSIGNED && index < lif->ionic->nintrs)
		clear_bit(index, lif->ionic->intrs);
}

static void ionic_lif_qcq_deinit(struct ionic_lif *lif, struct ionic_qcq *qcq)
{
	struct ionic_dev *idev = &lif->ionic->idev;
	struct device *dev = lif->ionic->dev;

	if (!qcq)
		return;

	ionic_debugfs_del_qcq(qcq);

	if (!(qcq->flags & IONIC_QCQ_F_INITED))
		return;

	if (qcq->flags & IONIC_QCQ_F_INTR) {
		ionic_intr_mask(idev->intr_ctrl, qcq->intr.index,
				IONIC_INTR_MASK_SET);
		devm_free_irq(dev, qcq->intr.vector, &qcq->napi);
		netif_napi_del(&qcq->napi);
	}

	qcq->flags &= ~IONIC_QCQ_F_INITED;
}

static void ionic_qcq_free(struct ionic_lif *lif, struct ionic_qcq *qcq)
{
	struct device *dev = lif->ionic->dev;

	if (!qcq)
		return;

	dma_free_coherent(dev, qcq->total_size, qcq->base, qcq->base_pa);
	qcq->base = NULL;
	qcq->base_pa = 0;

	if (qcq->flags & IONIC_QCQ_F_INTR)
		ionic_intr_free(lif, qcq->intr.index);

	devm_kfree(dev, qcq->cq.info);
	qcq->cq.info = NULL;
	devm_kfree(dev, qcq->q.info);
	qcq->q.info = NULL;
	devm_kfree(dev, qcq);
}

static void ionic_qcqs_free(struct ionic_lif *lif)
{
	if (lif->notifyqcq) {
		ionic_qcq_free(lif, lif->notifyqcq);
		lif->notifyqcq = NULL;
	}

	if (lif->adminqcq) {
		ionic_qcq_free(lif, lif->adminqcq);
		lif->adminqcq = NULL;
	}
}

static void ionic_link_qcq_interrupts(struct ionic_qcq *src_qcq,
				      struct ionic_qcq *n_qcq)
{
	if (WARN_ON(n_qcq->flags & IONIC_QCQ_F_INTR)) {
		ionic_intr_free(n_qcq->cq.lif, n_qcq->intr.index);
		n_qcq->flags &= ~IONIC_QCQ_F_INTR;
	}

	n_qcq->intr.vector = src_qcq->intr.vector;
	n_qcq->intr.index = src_qcq->intr.index;
}

static int ionic_qcq_alloc(struct ionic_lif *lif, unsigned int type,
			   unsigned int index,
			   const char *name, unsigned int flags,
			   unsigned int num_descs, unsigned int desc_size,
			   unsigned int cq_desc_size,
			   unsigned int sg_desc_size,
			   unsigned int pid, struct ionic_qcq **qcq)
{
	struct ionic_dev *idev = &lif->ionic->idev;
	u32 q_size, cq_size, sg_size, total_size;
	struct device *dev = lif->ionic->dev;
	void *q_base, *cq_base, *sg_base;
	dma_addr_t cq_base_pa = 0;
	dma_addr_t sg_base_pa = 0;
	dma_addr_t q_base_pa = 0;
	struct ionic_qcq *new;
	int err;

	*qcq = NULL;

	q_size  = num_descs * desc_size;
	cq_size = num_descs * cq_desc_size;
	sg_size = num_descs * sg_desc_size;

	total_size = ALIGN(q_size, PAGE_SIZE) + ALIGN(cq_size, PAGE_SIZE);
	/* Note: aligning q_size/cq_size is not enough due to cq_base
	 * address aligning as q_base could be not aligned to the page.
	 * Adding PAGE_SIZE.
	 */
	total_size += PAGE_SIZE;
	if (flags & IONIC_QCQ_F_SG) {
		total_size += ALIGN(sg_size, PAGE_SIZE);
		total_size += PAGE_SIZE;
	}

	new = devm_kzalloc(dev, sizeof(*new), GFP_KERNEL);
	if (!new) {
		netdev_err(lif->netdev, "Cannot allocate queue structure\n");
		err = -ENOMEM;
		goto err_out;
	}

	new->flags = flags;

	new->q.info = devm_kzalloc(dev, sizeof(*new->q.info) * num_descs,
				   GFP_KERNEL);
	if (!new->q.info) {
		netdev_err(lif->netdev, "Cannot allocate queue info\n");
		err = -ENOMEM;
		goto err_out;
	}

	new->q.type = type;

	err = ionic_q_init(lif, idev, &new->q, index, name, num_descs,
			   desc_size, sg_desc_size, pid);
	if (err) {
		netdev_err(lif->netdev, "Cannot initialize queue\n");
		goto err_out;
	}

	if (flags & IONIC_QCQ_F_INTR) {
		err = ionic_intr_alloc(lif, &new->intr);
		if (err) {
			netdev_warn(lif->netdev, "no intr for %s: %d\n",
				    name, err);
			goto err_out;
		}

		err = ionic_bus_get_irq(lif->ionic, new->intr.index);
		if (err < 0) {
			netdev_warn(lif->netdev, "no vector for %s: %d\n",
				    name, err);
			goto err_out_free_intr;
		}
		new->intr.vector = err;
		ionic_intr_mask_assert(idev->intr_ctrl, new->intr.index,
				       IONIC_INTR_MASK_SET);

		new->intr.cpu = new->intr.index % num_online_cpus();
		if (cpu_online(new->intr.cpu))
			cpumask_set_cpu(new->intr.cpu,
					&new->intr.affinity_mask);
	} else {
		new->intr.index = INTR_INDEX_NOT_ASSIGNED;
	}

	new->cq.info = devm_kzalloc(dev, sizeof(*new->cq.info) * num_descs,
				    GFP_KERNEL);
	if (!new->cq.info) {
		netdev_err(lif->netdev, "Cannot allocate completion queue info\n");
		err = -ENOMEM;
		goto err_out_free_intr;
	}

	err = ionic_cq_init(lif, &new->cq, &new->intr, num_descs, cq_desc_size);
	if (err) {
		netdev_err(lif->netdev, "Cannot initialize completion queue\n");
		goto err_out_free_intr;
	}

	new->base = dma_alloc_coherent(dev, total_size, &new->base_pa,
				       GFP_KERNEL);
	if (!new->base) {
		netdev_err(lif->netdev, "Cannot allocate queue DMA memory\n");
		err = -ENOMEM;
		goto err_out_free_intr;
	}

	new->total_size = total_size;

	q_base = new->base;
	q_base_pa = new->base_pa;

	cq_base = (void *)ALIGN((uintptr_t)q_base + q_size, PAGE_SIZE);
	cq_base_pa = ALIGN(q_base_pa + q_size, PAGE_SIZE);

	if (flags & IONIC_QCQ_F_SG) {
		sg_base = (void *)ALIGN((uintptr_t)cq_base + cq_size,
					PAGE_SIZE);
		sg_base_pa = ALIGN(cq_base_pa + cq_size, PAGE_SIZE);
		ionic_q_sg_map(&new->q, sg_base, sg_base_pa);
	}

	ionic_q_map(&new->q, q_base, q_base_pa);
	ionic_cq_map(&new->cq, cq_base, cq_base_pa);
	ionic_cq_bind(&new->cq, &new->q);

	*qcq = new;

	return 0;

err_out_free_intr:
	ionic_intr_free(lif, new->intr.index);
err_out:
	dev_err(dev, "qcq alloc of %s%d failed %d\n", name, index, err);
	return err;
}

static int ionic_qcqs_alloc(struct ionic_lif *lif)
{
	unsigned int flags;
	int err;

	flags = IONIC_QCQ_F_INTR;
	err = ionic_qcq_alloc(lif, IONIC_QTYPE_ADMINQ, 0, "admin", flags,
			      IONIC_ADMINQ_LENGTH,
			      sizeof(struct ionic_admin_cmd),
			      sizeof(struct ionic_admin_comp),
			      0, lif->kern_pid, &lif->adminqcq);
	if (err)
		return err;

	if (lif->ionic->nnqs_per_lif) {
		flags = IONIC_QCQ_F_NOTIFYQ;
		err = ionic_qcq_alloc(lif, IONIC_QTYPE_NOTIFYQ, 0, "notifyq",
				      flags, IONIC_NOTIFYQ_LENGTH,
				      sizeof(struct ionic_notifyq_cmd),
				      sizeof(union ionic_notifyq_comp),
				      0, lif->kern_pid, &lif->notifyqcq);
		if (err)
			goto err_out_free_adminqcq;

		/* Let the notifyq ride on the adminq interrupt */
		ionic_link_qcq_interrupts(lif->adminqcq, lif->notifyqcq);
	}

	return 0;

err_out_free_adminqcq:
	ionic_qcq_free(lif, lif->adminqcq);
	lif->adminqcq = NULL;

	return err;
}

static bool ionic_notifyq_service(struct ionic_cq *cq,
				  struct ionic_cq_info *cq_info)
{
	union ionic_notifyq_comp *comp = cq_info->cq_desc;
	struct net_device *netdev;
	struct ionic_queue *q;
	struct ionic_lif *lif;
	u64 eid;

	q = cq->bound_q;
	lif = q->info[0].cb_arg;
	netdev = lif->netdev;
	eid = le64_to_cpu(comp->event.eid);

	/* Have we run out of new completions to process? */
	if (eid <= lif->last_eid)
		return false;

	lif->last_eid = eid;

	dev_dbg(lif->ionic->dev, "notifyq event:\n");
	dynamic_hex_dump("event ", DUMP_PREFIX_OFFSET, 16, 1,
			 comp, sizeof(*comp), true);

	switch (le16_to_cpu(comp->event.ecode)) {
	case IONIC_EVENT_LINK_CHANGE:
		netdev_info(netdev, "Notifyq IONIC_EVENT_LINK_CHANGE eid=%lld\n",
			    eid);
		netdev_info(netdev,
			    "  link_status=%d link_speed=%d\n",
			    le16_to_cpu(comp->link_change.link_status),
			    le32_to_cpu(comp->link_change.link_speed));
		break;
	case IONIC_EVENT_RESET:
		netdev_info(netdev, "Notifyq IONIC_EVENT_RESET eid=%lld\n",
			    eid);
		netdev_info(netdev, "  reset_code=%d state=%d\n",
			    comp->reset.reset_code,
			    comp->reset.state);
		break;
	default:
		netdev_warn(netdev, "Notifyq unknown event ecode=%d eid=%lld\n",
			    comp->event.ecode, eid);
		break;
	}

	return true;
}

static int ionic_notifyq_clean(struct ionic_lif *lif, int budget)
{
	struct ionic_dev *idev = &lif->ionic->idev;
	struct ionic_cq *cq = &lif->notifyqcq->cq;
	u32 work_done;

	work_done = ionic_cq_service(cq, budget, ionic_notifyq_service,
				     NULL, NULL);
	if (work_done)
		ionic_intr_credits(idev->intr_ctrl, cq->bound_intr->index,
				   work_done, IONIC_INTR_CRED_RESET_COALESCE);

	return work_done;
}

static bool ionic_adminq_service(struct ionic_cq *cq,
				 struct ionic_cq_info *cq_info)
{
	struct ionic_admin_comp *comp = cq_info->cq_desc;

	if (!color_match(comp->color, cq->done_color))
		return false;

	ionic_q_service(cq->bound_q, cq_info, le16_to_cpu(comp->comp_index));

	return true;
}

static int ionic_adminq_napi(struct napi_struct *napi, int budget)
{
	struct ionic_lif *lif = napi_to_cq(napi)->lif;
	int n_work = 0;
	int a_work = 0;

	if (likely(lif->notifyqcq && lif->notifyqcq->flags & IONIC_QCQ_F_INITED))
		n_work = ionic_notifyq_clean(lif, budget);
	a_work = ionic_napi(napi, budget, ionic_adminq_service, NULL, NULL);

	return max(n_work, a_work);
}

static struct ionic_lif *ionic_lif_alloc(struct ionic *ionic, unsigned int index)
{
	struct device *dev = ionic->dev;
	struct net_device *netdev;
	struct ionic_lif *lif;
	int err;

	netdev = alloc_etherdev_mqs(sizeof(*lif),
				    ionic->ntxqs_per_lif, ionic->ntxqs_per_lif);
	if (!netdev) {
		dev_err(dev, "Cannot allocate netdev, aborting\n");
		return ERR_PTR(-ENOMEM);
	}

	SET_NETDEV_DEV(netdev, dev);

	lif = netdev_priv(netdev);
	lif->netdev = netdev;

	lif->neqs = ionic->neqs_per_lif;
	lif->nxqs = ionic->ntxqs_per_lif;

	lif->ionic = ionic;
	lif->index = index;

	snprintf(lif->name, sizeof(lif->name), "lif%u", index);

	spin_lock_init(&lif->adminq_lock);

	/* allocate lif info */
	lif->info_sz = ALIGN(sizeof(*lif->info), PAGE_SIZE);
	lif->info = dma_alloc_coherent(dev, lif->info_sz,
				       &lif->info_pa, GFP_KERNEL);
	if (!lif->info) {
		dev_err(dev, "Failed to allocate lif info, aborting\n");
		err = -ENOMEM;
		goto err_out_free_netdev;
	}

	/* allocate queues */
	err = ionic_qcqs_alloc(lif);
	if (err)
		goto err_out_free_lif_info;

	list_add_tail(&lif->list, &ionic->lifs);

	return lif;

err_out_free_lif_info:
	dma_free_coherent(dev, lif->info_sz, lif->info, lif->info_pa);
	lif->info = NULL;
	lif->info_pa = 0;
err_out_free_netdev:
	free_netdev(lif->netdev);
	lif = NULL;

	return ERR_PTR(err);
}

int ionic_lifs_alloc(struct ionic *ionic)
{
	struct ionic_lif *lif;

	INIT_LIST_HEAD(&ionic->lifs);

	/* only build the first lif, others are for later features */
	set_bit(0, ionic->lifbits);
	lif = ionic_lif_alloc(ionic, 0);

	return PTR_ERR_OR_ZERO(lif);
}

static void ionic_lif_reset(struct ionic_lif *lif)
{
	struct ionic_dev *idev = &lif->ionic->idev;

	mutex_lock(&lif->ionic->dev_cmd_lock);
	ionic_dev_cmd_lif_reset(idev, lif->index);
	ionic_dev_cmd_wait(lif->ionic, DEVCMD_TIMEOUT);
	mutex_unlock(&lif->ionic->dev_cmd_lock);
}

static void ionic_lif_free(struct ionic_lif *lif)
{
	struct device *dev = lif->ionic->dev;

	/* free queues */
	ionic_qcqs_free(lif);
	ionic_lif_reset(lif);

	/* free lif info */
	dma_free_coherent(dev, lif->info_sz, lif->info, lif->info_pa);
	lif->info = NULL;
	lif->info_pa = 0;

	/* unmap doorbell page */
	ionic_bus_unmap_dbpage(lif->ionic, lif->kern_dbpage);
	lif->kern_dbpage = NULL;
	kfree(lif->dbid_inuse);
	lif->dbid_inuse = NULL;

	/* free netdev & lif */
	ionic_debugfs_del_lif(lif);
	list_del(&lif->list);
	free_netdev(lif->netdev);
}

void ionic_lifs_free(struct ionic *ionic)
{
	struct list_head *cur, *tmp;
	struct ionic_lif *lif;

	list_for_each_safe(cur, tmp, &ionic->lifs) {
		lif = list_entry(cur, struct ionic_lif, list);

		ionic_lif_free(lif);
	}
}

static void ionic_lif_deinit(struct ionic_lif *lif)
{
	if (!test_bit(IONIC_LIF_INITED, lif->state))
		return;

	clear_bit(IONIC_LIF_INITED, lif->state);

	napi_disable(&lif->adminqcq->napi);
	ionic_lif_qcq_deinit(lif, lif->notifyqcq);
	ionic_lif_qcq_deinit(lif, lif->adminqcq);

	ionic_lif_reset(lif);
}

void ionic_lifs_deinit(struct ionic *ionic)
{
	struct list_head *cur, *tmp;
	struct ionic_lif *lif;

	list_for_each_safe(cur, tmp, &ionic->lifs) {
		lif = list_entry(cur, struct ionic_lif, list);
		ionic_lif_deinit(lif);
	}
}

static int ionic_lif_adminq_init(struct ionic_lif *lif)
{
	struct device *dev = lif->ionic->dev;
	struct ionic_q_init_comp comp;
	struct ionic_dev *idev;
	struct ionic_qcq *qcq;
	struct ionic_queue *q;
	int err;

	idev = &lif->ionic->idev;
	qcq = lif->adminqcq;
	q = &qcq->q;

	mutex_lock(&lif->ionic->dev_cmd_lock);
	ionic_dev_cmd_adminq_init(idev, qcq, lif->index, qcq->intr.index);
	err = ionic_dev_cmd_wait(lif->ionic, DEVCMD_TIMEOUT);
	ionic_dev_cmd_comp(idev, (union ionic_dev_cmd_comp *)&comp);
	mutex_unlock(&lif->ionic->dev_cmd_lock);
	if (err) {
		netdev_err(lif->netdev, "adminq init failed %d\n", err);
		return err;
	}

	q->hw_type = comp.hw_type;
	q->hw_index = le32_to_cpu(comp.hw_index);
	q->dbval = IONIC_DBELL_QID(q->hw_index);

	dev_dbg(dev, "adminq->hw_type %d\n", q->hw_type);
	dev_dbg(dev, "adminq->hw_index %d\n", q->hw_index);

	netif_napi_add(lif->netdev, &qcq->napi, ionic_adminq_napi,
		       NAPI_POLL_WEIGHT);

	err = ionic_request_irq(lif, qcq);
	if (err) {
		netdev_warn(lif->netdev, "adminq irq request failed %d\n", err);
		netif_napi_del(&qcq->napi);
		return err;
	}

	napi_enable(&qcq->napi);

	if (qcq->flags & IONIC_QCQ_F_INTR)
		ionic_intr_mask(idev->intr_ctrl, qcq->intr.index,
				IONIC_INTR_MASK_CLEAR);

	qcq->flags |= IONIC_QCQ_F_INITED;

	ionic_debugfs_add_qcq(lif, qcq);

	return 0;
}

static int ionic_lif_notifyq_init(struct ionic_lif *lif)
{
	struct ionic_qcq *qcq = lif->notifyqcq;
	struct device *dev = lif->ionic->dev;
	struct ionic_queue *q = &qcq->q;
	int err;

	struct ionic_admin_ctx ctx = {
		.work = COMPLETION_INITIALIZER_ONSTACK(ctx.work),
		.cmd.q_init = {
			.opcode = IONIC_CMD_Q_INIT,
			.lif_index = cpu_to_le16(lif->index),
			.type = q->type,
			.index = cpu_to_le32(q->index),
			.flags = cpu_to_le16(IONIC_QINIT_F_IRQ |
					     IONIC_QINIT_F_ENA),
			.intr_index = cpu_to_le16(lif->adminqcq->intr.index),
			.pid = cpu_to_le16(q->pid),
			.ring_size = ilog2(q->num_descs),
			.ring_base = cpu_to_le64(q->base_pa),
		}
	};

	dev_dbg(dev, "notifyq_init.pid %d\n", ctx.cmd.q_init.pid);
	dev_dbg(dev, "notifyq_init.index %d\n", ctx.cmd.q_init.index);
	dev_dbg(dev, "notifyq_init.ring_base 0x%llx\n", ctx.cmd.q_init.ring_base);
	dev_dbg(dev, "notifyq_init.ring_size %d\n", ctx.cmd.q_init.ring_size);

	err = ionic_adminq_post_wait(lif, &ctx);
	if (err)
		return err;

	q->hw_type = ctx.comp.q_init.hw_type;
	q->hw_index = le32_to_cpu(ctx.comp.q_init.hw_index);
	q->dbval = IONIC_DBELL_QID(q->hw_index);

	dev_dbg(dev, "notifyq->hw_type %d\n", q->hw_type);
	dev_dbg(dev, "notifyq->hw_index %d\n", q->hw_index);

	/* preset the callback info */
	q->info[0].cb_arg = lif;

	qcq->flags |= IONIC_QCQ_F_INITED;

	ionic_debugfs_add_qcq(lif, qcq);

	return 0;
}

static int ionic_lif_init(struct ionic_lif *lif)
{
	struct ionic_dev *idev = &lif->ionic->idev;
	struct device *dev = lif->ionic->dev;
	struct ionic_lif_init_comp comp;
	int dbpage_num;
	int err;

	ionic_debugfs_add_lif(lif);

	mutex_lock(&lif->ionic->dev_cmd_lock);
	ionic_dev_cmd_lif_init(idev, lif->index, lif->info_pa);
	err = ionic_dev_cmd_wait(lif->ionic, DEVCMD_TIMEOUT);
	ionic_dev_cmd_comp(idev, (union ionic_dev_cmd_comp *)&comp);
	mutex_unlock(&lif->ionic->dev_cmd_lock);
	if (err)
		return err;

	lif->hw_index = le16_to_cpu(comp.hw_index);

	/* now that we have the hw_index we can figure out our doorbell page */
	lif->dbid_count = le32_to_cpu(lif->ionic->ident.dev.ndbpgs_per_lif);
	if (!lif->dbid_count) {
		dev_err(dev, "No doorbell pages, aborting\n");
		return -EINVAL;
	}

	lif->dbid_inuse = bitmap_alloc(lif->dbid_count, GFP_KERNEL);
	if (!lif->dbid_inuse) {
		dev_err(dev, "Failed alloc doorbell id bitmap, aborting\n");
		return -ENOMEM;
	}

	/* first doorbell id reserved for kernel (dbid aka pid == zero) */
	set_bit(0, lif->dbid_inuse);
	lif->kern_pid = 0;

	dbpage_num = ionic_db_page_num(lif, lif->kern_pid);
	lif->kern_dbpage = ionic_bus_map_dbpage(lif->ionic, dbpage_num);
	if (!lif->kern_dbpage) {
		dev_err(dev, "Cannot map dbpage, aborting\n");
		err = -ENOMEM;
		goto err_out_free_dbid;
	}

	err = ionic_lif_adminq_init(lif);
	if (err)
		goto err_out_adminq_deinit;

	if (lif->ionic->nnqs_per_lif) {
		err = ionic_lif_notifyq_init(lif);
		if (err)
			goto err_out_notifyq_deinit;
	}

	set_bit(IONIC_LIF_INITED, lif->state);

	return 0;

err_out_notifyq_deinit:
	ionic_lif_qcq_deinit(lif, lif->notifyqcq);
err_out_adminq_deinit:
	ionic_lif_qcq_deinit(lif, lif->adminqcq);
	ionic_lif_reset(lif);
	ionic_bus_unmap_dbpage(lif->ionic, lif->kern_dbpage);
	lif->kern_dbpage = NULL;
err_out_free_dbid:
	kfree(lif->dbid_inuse);
	lif->dbid_inuse = NULL;

	return err;
}

int ionic_lifs_init(struct ionic *ionic)
{
	struct list_head *cur, *tmp;
	struct ionic_lif *lif;
	int err;

	list_for_each_safe(cur, tmp, &ionic->lifs) {
		lif = list_entry(cur, struct ionic_lif, list);
		err = ionic_lif_init(lif);
		if (err)
			return err;
	}

	return 0;
}

int ionic_lif_identify(struct ionic *ionic, u8 lif_type,
		       union ionic_lif_identity *lid)
{
	struct ionic_dev *idev = &ionic->idev;
	size_t sz;
	int err;

	sz = min(sizeof(*lid), sizeof(idev->dev_cmd_regs->data));

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_lif_identify(idev, lif_type, IONIC_IDENTITY_VERSION_1);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	memcpy_fromio(lid, &idev->dev_cmd_regs->data, sz);
	mutex_unlock(&ionic->dev_cmd_lock);
	if (err)
		return (err);

	dev_dbg(ionic->dev, "capabilities 0x%llx\n",
		le64_to_cpu(lid->capabilities));

	dev_dbg(ionic->dev, "eth.max_ucast_filters %d\n",
		le32_to_cpu(lid->eth.max_ucast_filters));
	dev_dbg(ionic->dev, "eth.max_mcast_filters %d\n",
		le32_to_cpu(lid->eth.max_mcast_filters));
	dev_dbg(ionic->dev, "eth.features 0x%llx\n",
		le64_to_cpu(lid->eth.config.features));
	dev_dbg(ionic->dev, "eth.queue_count[IONIC_QTYPE_ADMINQ] %d\n",
		le32_to_cpu(lid->eth.config.queue_count[IONIC_QTYPE_ADMINQ]));
	dev_dbg(ionic->dev, "eth.queue_count[IONIC_QTYPE_NOTIFYQ] %d\n",
		le32_to_cpu(lid->eth.config.queue_count[IONIC_QTYPE_NOTIFYQ]));
	dev_dbg(ionic->dev, "eth.queue_count[IONIC_QTYPE_RXQ] %d\n",
		le32_to_cpu(lid->eth.config.queue_count[IONIC_QTYPE_RXQ]));
	dev_dbg(ionic->dev, "eth.queue_count[IONIC_QTYPE_TXQ] %d\n",
		le32_to_cpu(lid->eth.config.queue_count[IONIC_QTYPE_TXQ]));
	dev_dbg(ionic->dev, "eth.config.name %s\n", lid->eth.config.name);
	dev_dbg(ionic->dev, "eth.config.mac %pM\n", lid->eth.config.mac);
	dev_dbg(ionic->dev, "eth.config.mtu %d\n",
		le32_to_cpu(lid->eth.config.mtu));

	return 0;
}

int ionic_lifs_size(struct ionic *ionic)
{
	struct ionic_identity *ident = &ionic->ident;
	unsigned int nintrs, dev_nintrs;
	union ionic_lif_config *lc;
	unsigned int ntxqs_per_lif;
	unsigned int nrxqs_per_lif;
	unsigned int neqs_per_lif;
	unsigned int nnqs_per_lif;
	unsigned int nxqs, neqs;
	unsigned int min_intrs;
	int err;

	lc = &ident->lif.eth.config;
	dev_nintrs = le32_to_cpu(ident->dev.nintrs);
	neqs_per_lif = le32_to_cpu(ident->lif.rdma.eq_qtype.qid_count);
	nnqs_per_lif = le32_to_cpu(lc->queue_count[IONIC_QTYPE_NOTIFYQ]);
	ntxqs_per_lif = le32_to_cpu(lc->queue_count[IONIC_QTYPE_TXQ]);
	nrxqs_per_lif = le32_to_cpu(lc->queue_count[IONIC_QTYPE_RXQ]);

	nxqs = min(ntxqs_per_lif, nrxqs_per_lif);
	nxqs = min(nxqs, num_online_cpus());
	neqs = min(neqs_per_lif, num_online_cpus());

try_again:
	/* interrupt usage:
	 *    1 for master lif adminq/notifyq
	 *    1 for each CPU for master lif TxRx queue pairs
	 *    whatever's left is for RDMA queues
	 */
	nintrs = 1 + nxqs + neqs;
	min_intrs = 2;  /* adminq + 1 TxRx queue pair */

	if (nintrs > dev_nintrs)
		goto try_fewer;

	err = ionic_bus_alloc_irq_vectors(ionic, nintrs);
	if (err < 0 && err != -ENOSPC) {
		dev_err(ionic->dev, "Can't get intrs from OS: %d\n", err);
		return err;
	}
	if (err == -ENOSPC)
		goto try_fewer;

	if (err != nintrs) {
		ionic_bus_free_irq_vectors(ionic);
		goto try_fewer;
	}

	ionic->nnqs_per_lif = nnqs_per_lif;
	ionic->neqs_per_lif = neqs;
	ionic->ntxqs_per_lif = nxqs;
	ionic->nrxqs_per_lif = nxqs;
	ionic->nintrs = nintrs;

	ionic_debugfs_add_sizes(ionic);

	return 0;

try_fewer:
	if (nnqs_per_lif > 1) {
		nnqs_per_lif >>= 1;
		goto try_again;
	}
	if (neqs > 1) {
		neqs >>= 1;
		goto try_again;
	}
	if (nxqs > 1) {
		nxqs >>= 1;
		goto try_again;
	}
	dev_err(ionic->dev, "Can't get minimum %d intrs from OS\n", min_intrs);
	return -ENOSPC;
}
