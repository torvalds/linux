// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pci.h>
#include <linux/vmalloc.h>

#include "core.h"

static BLOCKING_NOTIFIER_HEAD(pds_notify_chain);

int pdsc_register_notify(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&pds_notify_chain, nb);
}
EXPORT_SYMBOL_GPL(pdsc_register_notify);

void pdsc_unregister_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&pds_notify_chain, nb);
}
EXPORT_SYMBOL_GPL(pdsc_unregister_notify);

void pdsc_notify(unsigned long event, void *data)
{
	blocking_notifier_call_chain(&pds_notify_chain, event, data);
}

void pdsc_intr_free(struct pdsc *pdsc, int index)
{
	struct pdsc_intr_info *intr_info;

	if (index >= pdsc->nintrs || index < 0) {
		WARN(true, "bad intr index %d\n", index);
		return;
	}

	intr_info = &pdsc->intr_info[index];
	if (!intr_info->vector)
		return;
	dev_dbg(pdsc->dev, "%s: idx %d vec %d name %s\n",
		__func__, index, intr_info->vector, intr_info->name);

	pds_core_intr_mask(&pdsc->intr_ctrl[index], PDS_CORE_INTR_MASK_SET);
	pds_core_intr_clean(&pdsc->intr_ctrl[index]);

	free_irq(intr_info->vector, intr_info->data);

	memset(intr_info, 0, sizeof(*intr_info));
}

int pdsc_intr_alloc(struct pdsc *pdsc, char *name,
		    irq_handler_t handler, void *data)
{
	struct pdsc_intr_info *intr_info;
	unsigned int index;
	int err;

	/* Find the first available interrupt */
	for (index = 0; index < pdsc->nintrs; index++)
		if (!pdsc->intr_info[index].vector)
			break;
	if (index >= pdsc->nintrs) {
		dev_warn(pdsc->dev, "%s: no intr, index=%d nintrs=%d\n",
			 __func__, index, pdsc->nintrs);
		return -ENOSPC;
	}

	pds_core_intr_clean_flags(&pdsc->intr_ctrl[index],
				  PDS_CORE_INTR_CRED_RESET_COALESCE);

	intr_info = &pdsc->intr_info[index];

	intr_info->index = index;
	intr_info->data = data;
	strscpy(intr_info->name, name, sizeof(intr_info->name));

	/* Get the OS vector number for the interrupt */
	err = pci_irq_vector(pdsc->pdev, index);
	if (err < 0) {
		dev_err(pdsc->dev, "failed to get intr vector index %d: %pe\n",
			index, ERR_PTR(err));
		goto err_out_free_intr;
	}
	intr_info->vector = err;

	/* Init the device's intr mask */
	pds_core_intr_clean(&pdsc->intr_ctrl[index]);
	pds_core_intr_mask_assert(&pdsc->intr_ctrl[index], 1);
	pds_core_intr_mask(&pdsc->intr_ctrl[index], PDS_CORE_INTR_MASK_SET);

	/* Register the isr with a name */
	err = request_irq(intr_info->vector, handler, 0, intr_info->name, data);
	if (err) {
		dev_err(pdsc->dev, "failed to get intr irq vector %d: %pe\n",
			intr_info->vector, ERR_PTR(err));
		goto err_out_free_intr;
	}

	return index;

err_out_free_intr:
	pdsc_intr_free(pdsc, index);
	return err;
}

static void pdsc_qcq_intr_free(struct pdsc *pdsc, struct pdsc_qcq *qcq)
{
	if (!(qcq->flags & PDS_CORE_QCQ_F_INTR) ||
	    qcq->intx == PDS_CORE_INTR_INDEX_NOT_ASSIGNED)
		return;

	pdsc_intr_free(pdsc, qcq->intx);
	qcq->intx = PDS_CORE_INTR_INDEX_NOT_ASSIGNED;
}

static int pdsc_qcq_intr_alloc(struct pdsc *pdsc, struct pdsc_qcq *qcq)
{
	char name[PDSC_INTR_NAME_MAX_SZ];
	int index;

	if (!(qcq->flags & PDS_CORE_QCQ_F_INTR)) {
		qcq->intx = PDS_CORE_INTR_INDEX_NOT_ASSIGNED;
		return 0;
	}

	snprintf(name, sizeof(name), "%s-%d-%s",
		 PDS_CORE_DRV_NAME, pdsc->pdev->bus->number, qcq->q.name);
	index = pdsc_intr_alloc(pdsc, name, pdsc_adminq_isr, qcq);
	if (index < 0)
		return index;
	qcq->intx = index;

	return 0;
}

void pdsc_qcq_free(struct pdsc *pdsc, struct pdsc_qcq *qcq)
{
	struct device *dev = pdsc->dev;

	if (!(qcq && qcq->pdsc))
		return;

	pdsc_debugfs_del_qcq(qcq);

	pdsc_qcq_intr_free(pdsc, qcq);

	if (qcq->q_base)
		dma_free_coherent(dev, qcq->q_size,
				  qcq->q_base, qcq->q_base_pa);

	if (qcq->cq_base)
		dma_free_coherent(dev, qcq->cq_size,
				  qcq->cq_base, qcq->cq_base_pa);

	if (qcq->cq.info)
		vfree(qcq->cq.info);

	if (qcq->q.info)
		vfree(qcq->q.info);

	memset(qcq, 0, sizeof(*qcq));
}

static void pdsc_q_map(struct pdsc_queue *q, void *base, dma_addr_t base_pa)
{
	struct pdsc_q_info *cur;
	unsigned int i;

	q->base = base;
	q->base_pa = base_pa;

	for (i = 0, cur = q->info; i < q->num_descs; i++, cur++)
		cur->desc = base + (i * q->desc_size);
}

static void pdsc_cq_map(struct pdsc_cq *cq, void *base, dma_addr_t base_pa)
{
	struct pdsc_cq_info *cur;
	unsigned int i;

	cq->base = base;
	cq->base_pa = base_pa;

	for (i = 0, cur = cq->info; i < cq->num_descs; i++, cur++)
		cur->comp = base + (i * cq->desc_size);
}

int pdsc_qcq_alloc(struct pdsc *pdsc, unsigned int type, unsigned int index,
		   const char *name, unsigned int flags, unsigned int num_descs,
		   unsigned int desc_size, unsigned int cq_desc_size,
		   unsigned int pid, struct pdsc_qcq *qcq)
{
	struct device *dev = pdsc->dev;
	void *q_base, *cq_base;
	dma_addr_t cq_base_pa;
	dma_addr_t q_base_pa;
	int err;

	qcq->q.info = vcalloc(num_descs, sizeof(*qcq->q.info));
	if (!qcq->q.info) {
		err = -ENOMEM;
		goto err_out;
	}

	qcq->pdsc = pdsc;
	qcq->flags = flags;
	INIT_WORK(&qcq->work, pdsc_work_thread);

	qcq->q.type = type;
	qcq->q.index = index;
	qcq->q.num_descs = num_descs;
	qcq->q.desc_size = desc_size;
	qcq->q.tail_idx = 0;
	qcq->q.head_idx = 0;
	qcq->q.pid = pid;
	snprintf(qcq->q.name, sizeof(qcq->q.name), "%s%u", name, index);

	err = pdsc_qcq_intr_alloc(pdsc, qcq);
	if (err)
		goto err_out_free_q_info;

	qcq->cq.info = vcalloc(num_descs, sizeof(*qcq->cq.info));
	if (!qcq->cq.info) {
		err = -ENOMEM;
		goto err_out_free_irq;
	}

	qcq->cq.bound_intr = &pdsc->intr_info[qcq->intx];
	qcq->cq.num_descs = num_descs;
	qcq->cq.desc_size = cq_desc_size;
	qcq->cq.tail_idx = 0;
	qcq->cq.done_color = 1;

	if (flags & PDS_CORE_QCQ_F_NOTIFYQ) {
		/* q & cq need to be contiguous in case of notifyq */
		qcq->q_size = PDS_PAGE_SIZE +
			      ALIGN(num_descs * desc_size, PDS_PAGE_SIZE) +
			      ALIGN(num_descs * cq_desc_size, PDS_PAGE_SIZE);
		qcq->q_base = dma_alloc_coherent(dev,
						 qcq->q_size + qcq->cq_size,
						 &qcq->q_base_pa,
						 GFP_KERNEL);
		if (!qcq->q_base) {
			err = -ENOMEM;
			goto err_out_free_cq_info;
		}
		q_base = PTR_ALIGN(qcq->q_base, PDS_PAGE_SIZE);
		q_base_pa = ALIGN(qcq->q_base_pa, PDS_PAGE_SIZE);
		pdsc_q_map(&qcq->q, q_base, q_base_pa);

		cq_base = PTR_ALIGN(q_base +
				    ALIGN(num_descs * desc_size, PDS_PAGE_SIZE),
				    PDS_PAGE_SIZE);
		cq_base_pa = ALIGN(qcq->q_base_pa +
				   ALIGN(num_descs * desc_size, PDS_PAGE_SIZE),
				   PDS_PAGE_SIZE);

	} else {
		/* q DMA descriptors */
		qcq->q_size = PDS_PAGE_SIZE + (num_descs * desc_size);
		qcq->q_base = dma_alloc_coherent(dev, qcq->q_size,
						 &qcq->q_base_pa,
						 GFP_KERNEL);
		if (!qcq->q_base) {
			err = -ENOMEM;
			goto err_out_free_cq_info;
		}
		q_base = PTR_ALIGN(qcq->q_base, PDS_PAGE_SIZE);
		q_base_pa = ALIGN(qcq->q_base_pa, PDS_PAGE_SIZE);
		pdsc_q_map(&qcq->q, q_base, q_base_pa);

		/* cq DMA descriptors */
		qcq->cq_size = PDS_PAGE_SIZE + (num_descs * cq_desc_size);
		qcq->cq_base = dma_alloc_coherent(dev, qcq->cq_size,
						  &qcq->cq_base_pa,
						  GFP_KERNEL);
		if (!qcq->cq_base) {
			err = -ENOMEM;
			goto err_out_free_q;
		}
		cq_base = PTR_ALIGN(qcq->cq_base, PDS_PAGE_SIZE);
		cq_base_pa = ALIGN(qcq->cq_base_pa, PDS_PAGE_SIZE);
	}

	pdsc_cq_map(&qcq->cq, cq_base, cq_base_pa);
	qcq->cq.bound_q = &qcq->q;

	pdsc_debugfs_add_qcq(pdsc, qcq);

	return 0;

err_out_free_q:
	dma_free_coherent(dev, qcq->q_size, qcq->q_base, qcq->q_base_pa);
err_out_free_cq_info:
	vfree(qcq->cq.info);
err_out_free_irq:
	pdsc_qcq_intr_free(pdsc, qcq);
err_out_free_q_info:
	vfree(qcq->q.info);
	memset(qcq, 0, sizeof(*qcq));
err_out:
	dev_err(dev, "qcq alloc of %s%d failed %d\n", name, index, err);
	return err;
}

static int pdsc_core_init(struct pdsc *pdsc)
{
	union pds_core_dev_comp comp = {};
	union pds_core_dev_cmd cmd = {
		.init.opcode = PDS_CORE_CMD_INIT,
	};
	struct pds_core_dev_init_data_out cido;
	struct pds_core_dev_init_data_in cidi;
	u32 dbid_count;
	u32 dbpage_num;
	size_t sz;
	int err;

	cidi.adminq_q_base = cpu_to_le64(pdsc->adminqcq.q_base_pa);
	cidi.adminq_cq_base = cpu_to_le64(pdsc->adminqcq.cq_base_pa);
	cidi.notifyq_cq_base = cpu_to_le64(pdsc->notifyqcq.cq.base_pa);
	cidi.flags = cpu_to_le32(PDS_CORE_QINIT_F_IRQ | PDS_CORE_QINIT_F_ENA);
	cidi.intr_index = cpu_to_le16(pdsc->adminqcq.intx);
	cidi.adminq_ring_size = ilog2(pdsc->adminqcq.q.num_descs);
	cidi.notifyq_ring_size = ilog2(pdsc->notifyqcq.q.num_descs);

	mutex_lock(&pdsc->devcmd_lock);

	sz = min_t(size_t, sizeof(cidi), sizeof(pdsc->cmd_regs->data));
	memcpy_toio(&pdsc->cmd_regs->data, &cidi, sz);

	err = pdsc_devcmd_locked(pdsc, &cmd, &comp, pdsc->devcmd_timeout);
	if (!err) {
		sz = min_t(size_t, sizeof(cido), sizeof(pdsc->cmd_regs->data));
		memcpy_fromio(&cido, &pdsc->cmd_regs->data, sz);
	}

	mutex_unlock(&pdsc->devcmd_lock);
	if (err) {
		dev_err(pdsc->dev, "Device init command failed: %pe\n",
			ERR_PTR(err));
		return err;
	}

	pdsc->hw_index = le32_to_cpu(cido.core_hw_index);

	dbid_count = le32_to_cpu(pdsc->dev_ident.ndbpgs_per_lif);
	dbpage_num = pdsc->hw_index * dbid_count;
	pdsc->kern_dbpage = pdsc_map_dbpage(pdsc, dbpage_num);
	if (!pdsc->kern_dbpage) {
		dev_err(pdsc->dev, "Cannot map dbpage, aborting\n");
		return -ENOMEM;
	}

	pdsc->adminqcq.q.hw_type = cido.adminq_hw_type;
	pdsc->adminqcq.q.hw_index = le32_to_cpu(cido.adminq_hw_index);
	pdsc->adminqcq.q.dbval = PDS_CORE_DBELL_QID(pdsc->adminqcq.q.hw_index);

	pdsc->notifyqcq.q.hw_type = cido.notifyq_hw_type;
	pdsc->notifyqcq.q.hw_index = le32_to_cpu(cido.notifyq_hw_index);
	pdsc->notifyqcq.q.dbval = PDS_CORE_DBELL_QID(pdsc->notifyqcq.q.hw_index);

	pdsc->last_eid = 0;

	return err;
}

static struct pdsc_viftype pdsc_viftype_defaults[] = {
	[PDS_DEV_TYPE_VDPA] = { .name = PDS_DEV_TYPE_VDPA_STR,
				.vif_id = PDS_DEV_TYPE_VDPA,
				.dl_id = DEVLINK_PARAM_GENERIC_ID_ENABLE_VNET },
	[PDS_DEV_TYPE_MAX] = {}
};

static int pdsc_viftypes_init(struct pdsc *pdsc)
{
	enum pds_core_vif_types vt;

	pdsc->viftype_status = kzalloc(sizeof(pdsc_viftype_defaults),
				       GFP_KERNEL);
	if (!pdsc->viftype_status)
		return -ENOMEM;

	for (vt = 0; vt < PDS_DEV_TYPE_MAX; vt++) {
		bool vt_support;

		if (!pdsc_viftype_defaults[vt].name)
			continue;

		/* Grab the defaults */
		pdsc->viftype_status[vt] = pdsc_viftype_defaults[vt];

		/* See what the Core device has for support */
		vt_support = !!le16_to_cpu(pdsc->dev_ident.vif_types[vt]);
		dev_dbg(pdsc->dev, "VIF %s is %ssupported\n",
			pdsc->viftype_status[vt].name,
			vt_support ? "" : "not ");

		pdsc->viftype_status[vt].supported = vt_support;
	}

	return 0;
}

int pdsc_setup(struct pdsc *pdsc, bool init)
{
	int numdescs;
	int err;

	if (init)
		err = pdsc_dev_init(pdsc);
	else
		err = pdsc_dev_reinit(pdsc);
	if (err)
		return err;

	/* Scale the descriptor ring length based on number of CPUs and VFs */
	numdescs = max_t(int, PDSC_ADMINQ_MIN_LENGTH, num_online_cpus());
	numdescs += 2 * pci_sriov_get_totalvfs(pdsc->pdev);
	numdescs = roundup_pow_of_two(numdescs);
	err = pdsc_qcq_alloc(pdsc, PDS_CORE_QTYPE_ADMINQ, 0, "adminq",
			     PDS_CORE_QCQ_F_CORE | PDS_CORE_QCQ_F_INTR,
			     numdescs,
			     sizeof(union pds_core_adminq_cmd),
			     sizeof(union pds_core_adminq_comp),
			     0, &pdsc->adminqcq);
	if (err)
		goto err_out_teardown;

	err = pdsc_qcq_alloc(pdsc, PDS_CORE_QTYPE_NOTIFYQ, 0, "notifyq",
			     PDS_CORE_QCQ_F_NOTIFYQ,
			     PDSC_NOTIFYQ_LENGTH,
			     sizeof(struct pds_core_notifyq_cmd),
			     sizeof(union pds_core_notifyq_comp),
			     0, &pdsc->notifyqcq);
	if (err)
		goto err_out_teardown;

	/* NotifyQ rides on the AdminQ interrupt */
	pdsc->notifyqcq.intx = pdsc->adminqcq.intx;

	/* Set up the Core with the AdminQ and NotifyQ info */
	err = pdsc_core_init(pdsc);
	if (err)
		goto err_out_teardown;

	/* Set up the VIFs */
	err = pdsc_viftypes_init(pdsc);
	if (err)
		goto err_out_teardown;

	if (init)
		pdsc_debugfs_add_viftype(pdsc);

	clear_bit(PDSC_S_FW_DEAD, &pdsc->state);
	return 0;

err_out_teardown:
	pdsc_teardown(pdsc, init);
	return err;
}

void pdsc_teardown(struct pdsc *pdsc, bool removing)
{
	int i;

	pdsc_devcmd_reset(pdsc);
	pdsc_qcq_free(pdsc, &pdsc->notifyqcq);
	pdsc_qcq_free(pdsc, &pdsc->adminqcq);

	kfree(pdsc->viftype_status);
	pdsc->viftype_status = NULL;

	if (pdsc->intr_info) {
		for (i = 0; i < pdsc->nintrs; i++)
			pdsc_intr_free(pdsc, i);

		if (removing) {
			kfree(pdsc->intr_info);
			pdsc->intr_info = NULL;
		}
	}

	if (pdsc->kern_dbpage) {
		iounmap(pdsc->kern_dbpage);
		pdsc->kern_dbpage = NULL;
	}

	set_bit(PDSC_S_FW_DEAD, &pdsc->state);
}

int pdsc_start(struct pdsc *pdsc)
{
	pds_core_intr_mask(&pdsc->intr_ctrl[pdsc->adminqcq.intx],
			   PDS_CORE_INTR_MASK_CLEAR);

	return 0;
}

void pdsc_stop(struct pdsc *pdsc)
{
	int i;

	if (!pdsc->intr_info)
		return;

	/* Mask interrupts that are in use */
	for (i = 0; i < pdsc->nintrs; i++)
		if (pdsc->intr_info[i].vector)
			pds_core_intr_mask(&pdsc->intr_ctrl[i],
					   PDS_CORE_INTR_MASK_SET);
}

static void pdsc_fw_down(struct pdsc *pdsc)
{
	union pds_core_notifyq_comp reset_event = {
		.reset.ecode = cpu_to_le16(PDS_EVENT_RESET),
		.reset.state = 0,
	};

	if (test_and_set_bit(PDSC_S_FW_DEAD, &pdsc->state)) {
		dev_err(pdsc->dev, "%s: already happening\n", __func__);
		return;
	}

	/* Notify clients of fw_down */
	devlink_health_report(pdsc->fw_reporter, "FW down reported", pdsc);
	pdsc_notify(PDS_EVENT_RESET, &reset_event);

	pdsc_stop(pdsc);
	pdsc_teardown(pdsc, PDSC_TEARDOWN_RECOVERY);
}

static void pdsc_fw_up(struct pdsc *pdsc)
{
	union pds_core_notifyq_comp reset_event = {
		.reset.ecode = cpu_to_le16(PDS_EVENT_RESET),
		.reset.state = 1,
	};
	int err;

	if (!test_bit(PDSC_S_FW_DEAD, &pdsc->state)) {
		dev_err(pdsc->dev, "%s: fw not dead\n", __func__);
		return;
	}

	err = pdsc_setup(pdsc, PDSC_SETUP_RECOVERY);
	if (err)
		goto err_out;

	err = pdsc_start(pdsc);
	if (err)
		goto err_out;

	/* Notify clients of fw_up */
	pdsc->fw_recoveries++;
	devlink_health_reporter_state_update(pdsc->fw_reporter,
					     DEVLINK_HEALTH_REPORTER_STATE_HEALTHY);
	pdsc_notify(PDS_EVENT_RESET, &reset_event);

	return;

err_out:
	pdsc_teardown(pdsc, PDSC_TEARDOWN_RECOVERY);
}

void pdsc_health_thread(struct work_struct *work)
{
	struct pdsc *pdsc = container_of(work, struct pdsc, health_work);
	unsigned long mask;
	bool healthy;

	mutex_lock(&pdsc->config_lock);

	/* Don't do a check when in a transition state */
	mask = BIT_ULL(PDSC_S_INITING_DRIVER) |
	       BIT_ULL(PDSC_S_STOPPING_DRIVER);
	if (pdsc->state & mask)
		goto out_unlock;

	healthy = pdsc_is_fw_good(pdsc);
	dev_dbg(pdsc->dev, "%s: health %d fw_status %#02x fw_heartbeat %d\n",
		__func__, healthy, pdsc->fw_status, pdsc->last_hb);

	if (test_bit(PDSC_S_FW_DEAD, &pdsc->state)) {
		if (healthy)
			pdsc_fw_up(pdsc);
	} else {
		if (!healthy)
			pdsc_fw_down(pdsc);
	}

	pdsc->fw_generation = pdsc->fw_status & PDS_CORE_FW_STS_F_GENERATION;

out_unlock:
	mutex_unlock(&pdsc->config_lock);
}
