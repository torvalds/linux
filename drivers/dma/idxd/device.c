// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/dmaengine.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <uapi/linux/idxd.h>
#include "../dmaengine.h"
#include "idxd.h"
#include "registers.h"

static void idxd_cmd_exec(struct idxd_device *idxd, int cmd_code, u32 operand,
			  u32 *status);
static void idxd_device_wqs_clear_state(struct idxd_device *idxd);
static void idxd_wq_disable_cleanup(struct idxd_wq *wq);

/* Interrupt control bits */
void idxd_mask_msix_vector(struct idxd_device *idxd, int vec_id)
{
	struct irq_data *data = irq_get_irq_data(idxd->irq_entries[vec_id].vector);

	pci_msi_mask_irq(data);
}

void idxd_mask_msix_vectors(struct idxd_device *idxd)
{
	struct pci_dev *pdev = idxd->pdev;
	int msixcnt = pci_msix_vec_count(pdev);
	int i;

	for (i = 0; i < msixcnt; i++)
		idxd_mask_msix_vector(idxd, i);
}

void idxd_unmask_msix_vector(struct idxd_device *idxd, int vec_id)
{
	struct irq_data *data = irq_get_irq_data(idxd->irq_entries[vec_id].vector);

	pci_msi_unmask_irq(data);
}

void idxd_unmask_error_interrupts(struct idxd_device *idxd)
{
	union genctrl_reg genctrl;

	genctrl.bits = ioread32(idxd->reg_base + IDXD_GENCTRL_OFFSET);
	genctrl.softerr_int_en = 1;
	genctrl.halt_int_en = 1;
	iowrite32(genctrl.bits, idxd->reg_base + IDXD_GENCTRL_OFFSET);
}

void idxd_mask_error_interrupts(struct idxd_device *idxd)
{
	union genctrl_reg genctrl;

	genctrl.bits = ioread32(idxd->reg_base + IDXD_GENCTRL_OFFSET);
	genctrl.softerr_int_en = 0;
	genctrl.halt_int_en = 0;
	iowrite32(genctrl.bits, idxd->reg_base + IDXD_GENCTRL_OFFSET);
}

static void free_hw_descs(struct idxd_wq *wq)
{
	int i;

	for (i = 0; i < wq->num_descs; i++)
		kfree(wq->hw_descs[i]);

	kfree(wq->hw_descs);
}

static int alloc_hw_descs(struct idxd_wq *wq, int num)
{
	struct device *dev = &wq->idxd->pdev->dev;
	int i;
	int node = dev_to_node(dev);

	wq->hw_descs = kcalloc_node(num, sizeof(struct dsa_hw_desc *),
				    GFP_KERNEL, node);
	if (!wq->hw_descs)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		wq->hw_descs[i] = kzalloc_node(sizeof(*wq->hw_descs[i]),
					       GFP_KERNEL, node);
		if (!wq->hw_descs[i]) {
			free_hw_descs(wq);
			return -ENOMEM;
		}
	}

	return 0;
}

static void free_descs(struct idxd_wq *wq)
{
	int i;

	for (i = 0; i < wq->num_descs; i++)
		kfree(wq->descs[i]);

	kfree(wq->descs);
}

static int alloc_descs(struct idxd_wq *wq, int num)
{
	struct device *dev = &wq->idxd->pdev->dev;
	int i;
	int node = dev_to_node(dev);

	wq->descs = kcalloc_node(num, sizeof(struct idxd_desc *),
				 GFP_KERNEL, node);
	if (!wq->descs)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		wq->descs[i] = kzalloc_node(sizeof(*wq->descs[i]),
					    GFP_KERNEL, node);
		if (!wq->descs[i]) {
			free_descs(wq);
			return -ENOMEM;
		}
	}

	return 0;
}

/* WQ control bits */
int idxd_wq_alloc_resources(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	int rc, num_descs, i;
	int align;
	u64 tmp;

	if (wq->type != IDXD_WQT_KERNEL)
		return 0;

	num_descs = wq_dedicated(wq) ? wq->size : wq->threshold;
	wq->num_descs = num_descs;

	rc = alloc_hw_descs(wq, num_descs);
	if (rc < 0)
		return rc;

	align = idxd->data->align;
	wq->compls_size = num_descs * idxd->data->compl_size + align;
	wq->compls_raw = dma_alloc_coherent(dev, wq->compls_size,
					    &wq->compls_addr_raw, GFP_KERNEL);
	if (!wq->compls_raw) {
		rc = -ENOMEM;
		goto fail_alloc_compls;
	}

	/* Adjust alignment */
	wq->compls_addr = (wq->compls_addr_raw + (align - 1)) & ~(align - 1);
	tmp = (u64)wq->compls_raw;
	tmp = (tmp + (align - 1)) & ~(align - 1);
	wq->compls = (struct dsa_completion_record *)tmp;

	rc = alloc_descs(wq, num_descs);
	if (rc < 0)
		goto fail_alloc_descs;

	rc = sbitmap_queue_init_node(&wq->sbq, num_descs, -1, false, GFP_KERNEL,
				     dev_to_node(dev));
	if (rc < 0)
		goto fail_sbitmap_init;

	for (i = 0; i < num_descs; i++) {
		struct idxd_desc *desc = wq->descs[i];

		desc->hw = wq->hw_descs[i];
		if (idxd->data->type == IDXD_TYPE_DSA)
			desc->completion = &wq->compls[i];
		else if (idxd->data->type == IDXD_TYPE_IAX)
			desc->iax_completion = &wq->iax_compls[i];
		desc->compl_dma = wq->compls_addr + idxd->data->compl_size * i;
		desc->id = i;
		desc->wq = wq;
		desc->cpu = -1;
	}

	return 0;

 fail_sbitmap_init:
	free_descs(wq);
 fail_alloc_descs:
	dma_free_coherent(dev, wq->compls_size, wq->compls_raw,
			  wq->compls_addr_raw);
 fail_alloc_compls:
	free_hw_descs(wq);
	return rc;
}

void idxd_wq_free_resources(struct idxd_wq *wq)
{
	struct device *dev = &wq->idxd->pdev->dev;

	if (wq->type != IDXD_WQT_KERNEL)
		return;

	free_hw_descs(wq);
	free_descs(wq);
	dma_free_coherent(dev, wq->compls_size, wq->compls_raw,
			  wq->compls_addr_raw);
	sbitmap_queue_free(&wq->sbq);
}

int idxd_wq_enable(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	u32 status;

	if (wq->state == IDXD_WQ_ENABLED) {
		dev_dbg(dev, "WQ %d already enabled\n", wq->id);
		return -ENXIO;
	}

	idxd_cmd_exec(idxd, IDXD_CMD_ENABLE_WQ, wq->id, &status);

	if (status != IDXD_CMDSTS_SUCCESS &&
	    status != IDXD_CMDSTS_ERR_WQ_ENABLED) {
		dev_dbg(dev, "WQ enable failed: %#x\n", status);
		return -ENXIO;
	}

	wq->state = IDXD_WQ_ENABLED;
	dev_dbg(dev, "WQ %d enabled\n", wq->id);
	return 0;
}

int idxd_wq_disable(struct idxd_wq *wq, bool reset_config)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	u32 status, operand;

	dev_dbg(dev, "Disabling WQ %d\n", wq->id);

	if (wq->state != IDXD_WQ_ENABLED) {
		dev_dbg(dev, "WQ %d in wrong state: %d\n", wq->id, wq->state);
		return 0;
	}

	operand = BIT(wq->id % 16) | ((wq->id / 16) << 16);
	idxd_cmd_exec(idxd, IDXD_CMD_DISABLE_WQ, operand, &status);

	if (status != IDXD_CMDSTS_SUCCESS) {
		dev_dbg(dev, "WQ disable failed: %#x\n", status);
		return -ENXIO;
	}

	if (reset_config)
		idxd_wq_disable_cleanup(wq);
	wq->state = IDXD_WQ_DISABLED;
	dev_dbg(dev, "WQ %d disabled\n", wq->id);
	return 0;
}

void idxd_wq_drain(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	u32 operand;

	if (wq->state != IDXD_WQ_ENABLED) {
		dev_dbg(dev, "WQ %d in wrong state: %d\n", wq->id, wq->state);
		return;
	}

	dev_dbg(dev, "Draining WQ %d\n", wq->id);
	operand = BIT(wq->id % 16) | ((wq->id / 16) << 16);
	idxd_cmd_exec(idxd, IDXD_CMD_DRAIN_WQ, operand, NULL);
}

void idxd_wq_reset(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	u32 operand;

	if (wq->state != IDXD_WQ_ENABLED) {
		dev_dbg(dev, "WQ %d in wrong state: %d\n", wq->id, wq->state);
		return;
	}

	operand = BIT(wq->id % 16) | ((wq->id / 16) << 16);
	idxd_cmd_exec(idxd, IDXD_CMD_RESET_WQ, operand, NULL);
	idxd_wq_disable_cleanup(wq);
	wq->state = IDXD_WQ_DISABLED;
}

int idxd_wq_map_portal(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct pci_dev *pdev = idxd->pdev;
	struct device *dev = &pdev->dev;
	resource_size_t start;

	start = pci_resource_start(pdev, IDXD_WQ_BAR);
	start += idxd_get_wq_portal_full_offset(wq->id, IDXD_PORTAL_LIMITED);

	wq->portal = devm_ioremap(dev, start, IDXD_PORTAL_SIZE);
	if (!wq->portal)
		return -ENOMEM;

	return 0;
}

void idxd_wq_unmap_portal(struct idxd_wq *wq)
{
	struct device *dev = &wq->idxd->pdev->dev;

	devm_iounmap(dev, wq->portal);
	wq->portal = NULL;
	wq->portal_offset = 0;
}

void idxd_wqs_unmap_portal(struct idxd_device *idxd)
{
	int i;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		if (wq->portal)
			idxd_wq_unmap_portal(wq);
	}
}

int idxd_wq_set_pasid(struct idxd_wq *wq, int pasid)
{
	struct idxd_device *idxd = wq->idxd;
	int rc;
	union wqcfg wqcfg;
	unsigned int offset;

	rc = idxd_wq_disable(wq, false);
	if (rc < 0)
		return rc;

	offset = WQCFG_OFFSET(idxd, wq->id, WQCFG_PASID_IDX);
	spin_lock(&idxd->dev_lock);
	wqcfg.bits[WQCFG_PASID_IDX] = ioread32(idxd->reg_base + offset);
	wqcfg.pasid_en = 1;
	wqcfg.pasid = pasid;
	iowrite32(wqcfg.bits[WQCFG_PASID_IDX], idxd->reg_base + offset);
	spin_unlock(&idxd->dev_lock);

	rc = idxd_wq_enable(wq);
	if (rc < 0)
		return rc;

	return 0;
}

int idxd_wq_disable_pasid(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	int rc;
	union wqcfg wqcfg;
	unsigned int offset;

	rc = idxd_wq_disable(wq, false);
	if (rc < 0)
		return rc;

	offset = WQCFG_OFFSET(idxd, wq->id, WQCFG_PASID_IDX);
	spin_lock(&idxd->dev_lock);
	wqcfg.bits[WQCFG_PASID_IDX] = ioread32(idxd->reg_base + offset);
	wqcfg.pasid_en = 0;
	wqcfg.pasid = 0;
	iowrite32(wqcfg.bits[WQCFG_PASID_IDX], idxd->reg_base + offset);
	spin_unlock(&idxd->dev_lock);

	rc = idxd_wq_enable(wq);
	if (rc < 0)
		return rc;

	return 0;
}

static void idxd_wq_disable_cleanup(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;

	lockdep_assert_held(&wq->wq_lock);
	memset(wq->wqcfg, 0, idxd->wqcfg_size);
	wq->type = IDXD_WQT_NONE;
	wq->size = 0;
	wq->group = NULL;
	wq->threshold = 0;
	wq->priority = 0;
	wq->ats_dis = 0;
	clear_bit(WQ_FLAG_DEDICATED, &wq->flags);
	clear_bit(WQ_FLAG_BLOCK_ON_FAULT, &wq->flags);
	memset(wq->name, 0, WQ_NAME_SIZE);
}

static void idxd_wq_ref_release(struct percpu_ref *ref)
{
	struct idxd_wq *wq = container_of(ref, struct idxd_wq, wq_active);

	complete(&wq->wq_dead);
}

int idxd_wq_init_percpu_ref(struct idxd_wq *wq)
{
	int rc;

	memset(&wq->wq_active, 0, sizeof(wq->wq_active));
	rc = percpu_ref_init(&wq->wq_active, idxd_wq_ref_release, 0, GFP_KERNEL);
	if (rc < 0)
		return rc;
	reinit_completion(&wq->wq_dead);
	return 0;
}

void idxd_wq_quiesce(struct idxd_wq *wq)
{
	percpu_ref_kill(&wq->wq_active);
	wait_for_completion(&wq->wq_dead);
}

/* Device control bits */
static inline bool idxd_is_enabled(struct idxd_device *idxd)
{
	union gensts_reg gensts;

	gensts.bits = ioread32(idxd->reg_base + IDXD_GENSTATS_OFFSET);

	if (gensts.state == IDXD_DEVICE_STATE_ENABLED)
		return true;
	return false;
}

static inline bool idxd_device_is_halted(struct idxd_device *idxd)
{
	union gensts_reg gensts;

	gensts.bits = ioread32(idxd->reg_base + IDXD_GENSTATS_OFFSET);

	return (gensts.state == IDXD_DEVICE_STATE_HALT);
}

/*
 * This is function is only used for reset during probe and will
 * poll for completion. Once the device is setup with interrupts,
 * all commands will be done via interrupt completion.
 */
int idxd_device_init_reset(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	union idxd_command_reg cmd;

	if (idxd_device_is_halted(idxd)) {
		dev_warn(&idxd->pdev->dev, "Device is HALTED!\n");
		return -ENXIO;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = IDXD_CMD_RESET_DEVICE;
	dev_dbg(dev, "%s: sending reset for init.\n", __func__);
	spin_lock(&idxd->cmd_lock);
	iowrite32(cmd.bits, idxd->reg_base + IDXD_CMD_OFFSET);

	while (ioread32(idxd->reg_base + IDXD_CMDSTS_OFFSET) &
	       IDXD_CMDSTS_ACTIVE)
		cpu_relax();
	spin_unlock(&idxd->cmd_lock);
	return 0;
}

static void idxd_cmd_exec(struct idxd_device *idxd, int cmd_code, u32 operand,
			  u32 *status)
{
	union idxd_command_reg cmd;
	DECLARE_COMPLETION_ONSTACK(done);
	u32 stat;

	if (idxd_device_is_halted(idxd)) {
		dev_warn(&idxd->pdev->dev, "Device is HALTED!\n");
		if (status)
			*status = IDXD_CMDSTS_HW_ERR;
		return;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = cmd_code;
	cmd.operand = operand;
	cmd.int_req = 1;

	spin_lock(&idxd->cmd_lock);
	wait_event_lock_irq(idxd->cmd_waitq,
			    !test_bit(IDXD_FLAG_CMD_RUNNING, &idxd->flags),
			    idxd->cmd_lock);

	dev_dbg(&idxd->pdev->dev, "%s: sending cmd: %#x op: %#x\n",
		__func__, cmd_code, operand);

	idxd->cmd_status = 0;
	__set_bit(IDXD_FLAG_CMD_RUNNING, &idxd->flags);
	idxd->cmd_done = &done;
	iowrite32(cmd.bits, idxd->reg_base + IDXD_CMD_OFFSET);

	/*
	 * After command submitted, release lock and go to sleep until
	 * the command completes via interrupt.
	 */
	spin_unlock(&idxd->cmd_lock);
	wait_for_completion(&done);
	stat = ioread32(idxd->reg_base + IDXD_CMDSTS_OFFSET);
	spin_lock(&idxd->cmd_lock);
	if (status)
		*status = stat;
	idxd->cmd_status = stat & GENMASK(7, 0);

	__clear_bit(IDXD_FLAG_CMD_RUNNING, &idxd->flags);
	/* Wake up other pending commands */
	wake_up(&idxd->cmd_waitq);
	spin_unlock(&idxd->cmd_lock);
}

int idxd_device_enable(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	u32 status;

	if (idxd_is_enabled(idxd)) {
		dev_dbg(dev, "Device already enabled\n");
		return -ENXIO;
	}

	idxd_cmd_exec(idxd, IDXD_CMD_ENABLE_DEVICE, 0, &status);

	/* If the command is successful or if the device was enabled */
	if (status != IDXD_CMDSTS_SUCCESS &&
	    status != IDXD_CMDSTS_ERR_DEV_ENABLED) {
		dev_dbg(dev, "%s: err_code: %#x\n", __func__, status);
		return -ENXIO;
	}

	idxd->state = IDXD_DEV_ENABLED;
	return 0;
}

int idxd_device_disable(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	u32 status;

	if (!idxd_is_enabled(idxd)) {
		dev_dbg(dev, "Device is not enabled\n");
		return 0;
	}

	idxd_cmd_exec(idxd, IDXD_CMD_DISABLE_DEVICE, 0, &status);

	/* If the command is successful or if the device was disabled */
	if (status != IDXD_CMDSTS_SUCCESS &&
	    !(status & IDXD_CMDSTS_ERR_DIS_DEV_EN)) {
		dev_dbg(dev, "%s: err_code: %#x\n", __func__, status);
		return -ENXIO;
	}

	spin_lock(&idxd->dev_lock);
	idxd_device_clear_state(idxd);
	idxd->state = IDXD_DEV_DISABLED;
	spin_unlock(&idxd->dev_lock);
	return 0;
}

void idxd_device_reset(struct idxd_device *idxd)
{
	idxd_cmd_exec(idxd, IDXD_CMD_RESET_DEVICE, 0, NULL);
	spin_lock(&idxd->dev_lock);
	idxd_device_clear_state(idxd);
	idxd->state = IDXD_DEV_DISABLED;
	idxd_unmask_error_interrupts(idxd);
	idxd_msix_perm_setup(idxd);
	spin_unlock(&idxd->dev_lock);
}

void idxd_device_drain_pasid(struct idxd_device *idxd, int pasid)
{
	struct device *dev = &idxd->pdev->dev;
	u32 operand;

	operand = pasid;
	dev_dbg(dev, "cmd: %u operand: %#x\n", IDXD_CMD_DRAIN_PASID, operand);
	idxd_cmd_exec(idxd, IDXD_CMD_DRAIN_PASID, operand, NULL);
	dev_dbg(dev, "pasid %d drained\n", pasid);
}

int idxd_device_request_int_handle(struct idxd_device *idxd, int idx, int *handle,
				   enum idxd_interrupt_type irq_type)
{
	struct device *dev = &idxd->pdev->dev;
	u32 operand, status;

	if (!(idxd->hw.cmd_cap & BIT(IDXD_CMD_REQUEST_INT_HANDLE)))
		return -EOPNOTSUPP;

	dev_dbg(dev, "get int handle, idx %d\n", idx);

	operand = idx & GENMASK(15, 0);
	if (irq_type == IDXD_IRQ_IMS)
		operand |= CMD_INT_HANDLE_IMS;

	dev_dbg(dev, "cmd: %u operand: %#x\n", IDXD_CMD_REQUEST_INT_HANDLE, operand);

	idxd_cmd_exec(idxd, IDXD_CMD_REQUEST_INT_HANDLE, operand, &status);

	if ((status & IDXD_CMDSTS_ERR_MASK) != IDXD_CMDSTS_SUCCESS) {
		dev_dbg(dev, "request int handle failed: %#x\n", status);
		return -ENXIO;
	}

	*handle = (status >> IDXD_CMDSTS_RES_SHIFT) & GENMASK(15, 0);

	dev_dbg(dev, "int handle acquired: %u\n", *handle);
	return 0;
}

int idxd_device_release_int_handle(struct idxd_device *idxd, int handle,
				   enum idxd_interrupt_type irq_type)
{
	struct device *dev = &idxd->pdev->dev;
	u32 operand, status;
	union idxd_command_reg cmd;

	if (!(idxd->hw.cmd_cap & BIT(IDXD_CMD_RELEASE_INT_HANDLE)))
		return -EOPNOTSUPP;

	dev_dbg(dev, "release int handle, handle %d\n", handle);

	memset(&cmd, 0, sizeof(cmd));
	operand = handle & GENMASK(15, 0);

	if (irq_type == IDXD_IRQ_IMS)
		operand |= CMD_INT_HANDLE_IMS;

	cmd.cmd = IDXD_CMD_RELEASE_INT_HANDLE;
	cmd.operand = operand;

	dev_dbg(dev, "cmd: %u operand: %#x\n", IDXD_CMD_RELEASE_INT_HANDLE, operand);

	spin_lock(&idxd->cmd_lock);
	iowrite32(cmd.bits, idxd->reg_base + IDXD_CMD_OFFSET);

	while (ioread32(idxd->reg_base + IDXD_CMDSTS_OFFSET) & IDXD_CMDSTS_ACTIVE)
		cpu_relax();
	status = ioread32(idxd->reg_base + IDXD_CMDSTS_OFFSET);
	spin_unlock(&idxd->cmd_lock);

	if ((status & IDXD_CMDSTS_ERR_MASK) != IDXD_CMDSTS_SUCCESS) {
		dev_dbg(dev, "release int handle failed: %#x\n", status);
		return -ENXIO;
	}

	dev_dbg(dev, "int handle released.\n");
	return 0;
}

/* Device configuration bits */
static void idxd_engines_clear_state(struct idxd_device *idxd)
{
	struct idxd_engine *engine;
	int i;

	lockdep_assert_held(&idxd->dev_lock);
	for (i = 0; i < idxd->max_engines; i++) {
		engine = idxd->engines[i];
		engine->group = NULL;
	}
}

static void idxd_groups_clear_state(struct idxd_device *idxd)
{
	struct idxd_group *group;
	int i;

	lockdep_assert_held(&idxd->dev_lock);
	for (i = 0; i < idxd->max_groups; i++) {
		group = idxd->groups[i];
		memset(&group->grpcfg, 0, sizeof(group->grpcfg));
		group->num_engines = 0;
		group->num_wqs = 0;
		group->use_token_limit = false;
		group->tokens_allowed = 0;
		group->tokens_reserved = 0;
		group->tc_a = -1;
		group->tc_b = -1;
	}
}

static void idxd_device_wqs_clear_state(struct idxd_device *idxd)
{
	int i;

	lockdep_assert_held(&idxd->dev_lock);
	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		if (wq->state == IDXD_WQ_ENABLED) {
			idxd_wq_disable_cleanup(wq);
			wq->state = IDXD_WQ_DISABLED;
		}
	}
}

void idxd_device_clear_state(struct idxd_device *idxd)
{
	idxd_groups_clear_state(idxd);
	idxd_engines_clear_state(idxd);
	idxd_device_wqs_clear_state(idxd);
}

void idxd_msix_perm_setup(struct idxd_device *idxd)
{
	union msix_perm mperm;
	int i, msixcnt;

	msixcnt = pci_msix_vec_count(idxd->pdev);
	if (msixcnt < 0)
		return;

	mperm.bits = 0;
	mperm.pasid = idxd->pasid;
	mperm.pasid_en = device_pasid_enabled(idxd);
	for (i = 1; i < msixcnt; i++)
		iowrite32(mperm.bits, idxd->reg_base + idxd->msix_perm_offset + i * 8);
}

void idxd_msix_perm_clear(struct idxd_device *idxd)
{
	union msix_perm mperm;
	int i, msixcnt;

	msixcnt = pci_msix_vec_count(idxd->pdev);
	if (msixcnt < 0)
		return;

	mperm.bits = 0;
	for (i = 1; i < msixcnt; i++)
		iowrite32(mperm.bits, idxd->reg_base + idxd->msix_perm_offset + i * 8);
}

static void idxd_group_config_write(struct idxd_group *group)
{
	struct idxd_device *idxd = group->idxd;
	struct device *dev = &idxd->pdev->dev;
	int i;
	u32 grpcfg_offset;

	dev_dbg(dev, "Writing group %d cfg registers\n", group->id);

	/* setup GRPWQCFG */
	for (i = 0; i < GRPWQCFG_STRIDES; i++) {
		grpcfg_offset = GRPWQCFG_OFFSET(idxd, group->id, i);
		iowrite64(group->grpcfg.wqs[i], idxd->reg_base + grpcfg_offset);
		dev_dbg(dev, "GRPCFG wq[%d:%d: %#x]: %#llx\n",
			group->id, i, grpcfg_offset,
			ioread64(idxd->reg_base + grpcfg_offset));
	}

	/* setup GRPENGCFG */
	grpcfg_offset = GRPENGCFG_OFFSET(idxd, group->id);
	iowrite64(group->grpcfg.engines, idxd->reg_base + grpcfg_offset);
	dev_dbg(dev, "GRPCFG engs[%d: %#x]: %#llx\n", group->id,
		grpcfg_offset, ioread64(idxd->reg_base + grpcfg_offset));

	/* setup GRPFLAGS */
	grpcfg_offset = GRPFLGCFG_OFFSET(idxd, group->id);
	iowrite32(group->grpcfg.flags.bits, idxd->reg_base + grpcfg_offset);
	dev_dbg(dev, "GRPFLAGS flags[%d: %#x]: %#x\n",
		group->id, grpcfg_offset,
		ioread32(idxd->reg_base + grpcfg_offset));
}

static int idxd_groups_config_write(struct idxd_device *idxd)

{
	union gencfg_reg reg;
	int i;
	struct device *dev = &idxd->pdev->dev;

	/* Setup bandwidth token limit */
	if (idxd->hw.gen_cap.config_en && idxd->token_limit) {
		reg.bits = ioread32(idxd->reg_base + IDXD_GENCFG_OFFSET);
		reg.token_limit = idxd->token_limit;
		iowrite32(reg.bits, idxd->reg_base + IDXD_GENCFG_OFFSET);
	}

	dev_dbg(dev, "GENCFG(%#x): %#x\n", IDXD_GENCFG_OFFSET,
		ioread32(idxd->reg_base + IDXD_GENCFG_OFFSET));

	for (i = 0; i < idxd->max_groups; i++) {
		struct idxd_group *group = idxd->groups[i];

		idxd_group_config_write(group);
	}

	return 0;
}

static bool idxd_device_pasid_priv_enabled(struct idxd_device *idxd)
{
	struct pci_dev *pdev = idxd->pdev;

	if (pdev->pasid_enabled && (pdev->pasid_features & PCI_PASID_CAP_PRIV))
		return true;
	return false;
}

static int idxd_wq_config_write(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	u32 wq_offset;
	int i;

	if (!wq->group)
		return 0;

	/*
	 * Instead of memset the entire shadow copy of WQCFG, copy from the hardware after
	 * wq reset. This will copy back the sticky values that are present on some devices.
	 */
	for (i = 0; i < WQCFG_STRIDES(idxd); i++) {
		wq_offset = WQCFG_OFFSET(idxd, wq->id, i);
		wq->wqcfg->bits[i] = ioread32(idxd->reg_base + wq_offset);
	}

	/* byte 0-3 */
	wq->wqcfg->wq_size = wq->size;

	if (wq->size == 0) {
		idxd->cmd_status = IDXD_SCMD_WQ_NO_SIZE;
		dev_warn(dev, "Incorrect work queue size: 0\n");
		return -EINVAL;
	}

	/* bytes 4-7 */
	wq->wqcfg->wq_thresh = wq->threshold;

	/* byte 8-11 */
	if (wq_dedicated(wq))
		wq->wqcfg->mode = 1;

	if (device_pasid_enabled(idxd)) {
		wq->wqcfg->pasid_en = 1;
		if (wq->type == IDXD_WQT_KERNEL && wq_dedicated(wq))
			wq->wqcfg->pasid = idxd->pasid;
	}

	/*
	 * Here the priv bit is set depending on the WQ type. priv = 1 if the
	 * WQ type is kernel to indicate privileged access. This setting only
	 * matters for dedicated WQ. According to the DSA spec:
	 * If the WQ is in dedicated mode, WQ PASID Enable is 1, and the
	 * Privileged Mode Enable field of the PCI Express PASID capability
	 * is 0, this field must be 0.
	 *
	 * In the case of a dedicated kernel WQ that is not able to support
	 * the PASID cap, then the configuration will be rejected.
	 */
	wq->wqcfg->priv = !!(wq->type == IDXD_WQT_KERNEL);
	if (wq_dedicated(wq) && wq->wqcfg->pasid_en &&
	    !idxd_device_pasid_priv_enabled(idxd) &&
	    wq->type == IDXD_WQT_KERNEL) {
		idxd->cmd_status = IDXD_SCMD_WQ_NO_PRIV;
		return -EOPNOTSUPP;
	}

	wq->wqcfg->priority = wq->priority;

	if (idxd->hw.gen_cap.block_on_fault &&
	    test_bit(WQ_FLAG_BLOCK_ON_FAULT, &wq->flags))
		wq->wqcfg->bof = 1;

	if (idxd->hw.wq_cap.wq_ats_support)
		wq->wqcfg->wq_ats_disable = wq->ats_dis;

	/* bytes 12-15 */
	wq->wqcfg->max_xfer_shift = ilog2(wq->max_xfer_bytes);
	wq->wqcfg->max_batch_shift = ilog2(wq->max_batch_size);

	dev_dbg(dev, "WQ %d CFGs\n", wq->id);
	for (i = 0; i < WQCFG_STRIDES(idxd); i++) {
		wq_offset = WQCFG_OFFSET(idxd, wq->id, i);
		iowrite32(wq->wqcfg->bits[i], idxd->reg_base + wq_offset);
		dev_dbg(dev, "WQ[%d][%d][%#x]: %#x\n",
			wq->id, i, wq_offset,
			ioread32(idxd->reg_base + wq_offset));
	}

	return 0;
}

static int idxd_wqs_config_write(struct idxd_device *idxd)
{
	int i, rc;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		rc = idxd_wq_config_write(wq);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static void idxd_group_flags_setup(struct idxd_device *idxd)
{
	int i;

	/* TC-A 0 and TC-B 1 should be defaults */
	for (i = 0; i < idxd->max_groups; i++) {
		struct idxd_group *group = idxd->groups[i];

		if (group->tc_a == -1)
			group->tc_a = group->grpcfg.flags.tc_a = 0;
		else
			group->grpcfg.flags.tc_a = group->tc_a;
		if (group->tc_b == -1)
			group->tc_b = group->grpcfg.flags.tc_b = 1;
		else
			group->grpcfg.flags.tc_b = group->tc_b;
		group->grpcfg.flags.use_token_limit = group->use_token_limit;
		group->grpcfg.flags.tokens_reserved = group->tokens_reserved;
		if (group->tokens_allowed)
			group->grpcfg.flags.tokens_allowed =
				group->tokens_allowed;
		else
			group->grpcfg.flags.tokens_allowed = idxd->max_tokens;
	}
}

static int idxd_engines_setup(struct idxd_device *idxd)
{
	int i, engines = 0;
	struct idxd_engine *eng;
	struct idxd_group *group;

	for (i = 0; i < idxd->max_groups; i++) {
		group = idxd->groups[i];
		group->grpcfg.engines = 0;
	}

	for (i = 0; i < idxd->max_engines; i++) {
		eng = idxd->engines[i];
		group = eng->group;

		if (!group)
			continue;

		group->grpcfg.engines |= BIT(eng->id);
		engines++;
	}

	if (!engines)
		return -EINVAL;

	return 0;
}

static int idxd_wqs_setup(struct idxd_device *idxd)
{
	struct idxd_wq *wq;
	struct idxd_group *group;
	int i, j, configured = 0;
	struct device *dev = &idxd->pdev->dev;

	for (i = 0; i < idxd->max_groups; i++) {
		group = idxd->groups[i];
		for (j = 0; j < 4; j++)
			group->grpcfg.wqs[j] = 0;
	}

	for (i = 0; i < idxd->max_wqs; i++) {
		wq = idxd->wqs[i];
		group = wq->group;

		if (!wq->group)
			continue;
		if (!wq->size)
			continue;

		if (wq_shared(wq) && !device_swq_supported(idxd)) {
			idxd->cmd_status = IDXD_SCMD_WQ_NO_SWQ_SUPPORT;
			dev_warn(dev, "No shared wq support but configured.\n");
			return -EINVAL;
		}

		group->grpcfg.wqs[wq->id / 64] |= BIT(wq->id % 64);
		configured++;
	}

	if (configured == 0) {
		idxd->cmd_status = IDXD_SCMD_WQ_NONE_CONFIGURED;
		return -EINVAL;
	}

	return 0;
}

int idxd_device_config(struct idxd_device *idxd)
{
	int rc;

	lockdep_assert_held(&idxd->dev_lock);
	rc = idxd_wqs_setup(idxd);
	if (rc < 0)
		return rc;

	rc = idxd_engines_setup(idxd);
	if (rc < 0)
		return rc;

	idxd_group_flags_setup(idxd);

	rc = idxd_wqs_config_write(idxd);
	if (rc < 0)
		return rc;

	rc = idxd_groups_config_write(idxd);
	if (rc < 0)
		return rc;

	return 0;
}

static int idxd_wq_load_config(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	int wqcfg_offset;
	int i;

	wqcfg_offset = WQCFG_OFFSET(idxd, wq->id, 0);
	memcpy_fromio(wq->wqcfg, idxd->reg_base + wqcfg_offset, idxd->wqcfg_size);

	wq->size = wq->wqcfg->wq_size;
	wq->threshold = wq->wqcfg->wq_thresh;

	/* The driver does not support shared WQ mode in read-only config yet */
	if (wq->wqcfg->mode == 0 || wq->wqcfg->pasid_en)
		return -EOPNOTSUPP;

	set_bit(WQ_FLAG_DEDICATED, &wq->flags);

	wq->priority = wq->wqcfg->priority;

	for (i = 0; i < WQCFG_STRIDES(idxd); i++) {
		wqcfg_offset = WQCFG_OFFSET(idxd, wq->id, i);
		dev_dbg(dev, "WQ[%d][%d][%#x]: %#x\n", wq->id, i, wqcfg_offset, wq->wqcfg->bits[i]);
	}

	return 0;
}

static void idxd_group_load_config(struct idxd_group *group)
{
	struct idxd_device *idxd = group->idxd;
	struct device *dev = &idxd->pdev->dev;
	int i, j, grpcfg_offset;

	/*
	 * Load WQS bit fields
	 * Iterate through all 256 bits 64 bits at a time
	 */
	for (i = 0; i < GRPWQCFG_STRIDES; i++) {
		struct idxd_wq *wq;

		grpcfg_offset = GRPWQCFG_OFFSET(idxd, group->id, i);
		group->grpcfg.wqs[i] = ioread64(idxd->reg_base + grpcfg_offset);
		dev_dbg(dev, "GRPCFG wq[%d:%d: %#x]: %#llx\n",
			group->id, i, grpcfg_offset, group->grpcfg.wqs[i]);

		if (i * 64 >= idxd->max_wqs)
			break;

		/* Iterate through all 64 bits and check for wq set */
		for (j = 0; j < 64; j++) {
			int id = i * 64 + j;

			/* No need to check beyond max wqs */
			if (id >= idxd->max_wqs)
				break;

			/* Set group assignment for wq if wq bit is set */
			if (group->grpcfg.wqs[i] & BIT(j)) {
				wq = idxd->wqs[id];
				wq->group = group;
			}
		}
	}

	grpcfg_offset = GRPENGCFG_OFFSET(idxd, group->id);
	group->grpcfg.engines = ioread64(idxd->reg_base + grpcfg_offset);
	dev_dbg(dev, "GRPCFG engs[%d: %#x]: %#llx\n", group->id,
		grpcfg_offset, group->grpcfg.engines);

	/* Iterate through all 64 bits to check engines set */
	for (i = 0; i < 64; i++) {
		if (i >= idxd->max_engines)
			break;

		if (group->grpcfg.engines & BIT(i)) {
			struct idxd_engine *engine = idxd->engines[i];

			engine->group = group;
		}
	}

	grpcfg_offset = GRPFLGCFG_OFFSET(idxd, group->id);
	group->grpcfg.flags.bits = ioread32(idxd->reg_base + grpcfg_offset);
	dev_dbg(dev, "GRPFLAGS flags[%d: %#x]: %#x\n",
		group->id, grpcfg_offset, group->grpcfg.flags.bits);
}

int idxd_device_load_config(struct idxd_device *idxd)
{
	union gencfg_reg reg;
	int i, rc;

	reg.bits = ioread32(idxd->reg_base + IDXD_GENCFG_OFFSET);
	idxd->token_limit = reg.token_limit;

	for (i = 0; i < idxd->max_groups; i++) {
		struct idxd_group *group = idxd->groups[i];

		idxd_group_load_config(group);
	}

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];

		rc = idxd_wq_load_config(wq);
		if (rc < 0)
			return rc;
	}

	return 0;
}

int __drv_enable_wq(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	int rc = -ENXIO;

	lockdep_assert_held(&wq->wq_lock);

	if (idxd->state != IDXD_DEV_ENABLED) {
		idxd->cmd_status = IDXD_SCMD_DEV_NOT_ENABLED;
		goto err;
	}

	if (wq->state != IDXD_WQ_DISABLED) {
		dev_dbg(dev, "wq %d already enabled.\n", wq->id);
		idxd->cmd_status = IDXD_SCMD_WQ_ENABLED;
		rc = -EBUSY;
		goto err;
	}

	if (!wq->group) {
		dev_dbg(dev, "wq %d not attached to group.\n", wq->id);
		idxd->cmd_status = IDXD_SCMD_WQ_NO_GRP;
		goto err;
	}

	if (strlen(wq->name) == 0) {
		idxd->cmd_status = IDXD_SCMD_WQ_NO_NAME;
		dev_dbg(dev, "wq %d name not set.\n", wq->id);
		goto err;
	}

	/* Shared WQ checks */
	if (wq_shared(wq)) {
		if (!device_swq_supported(idxd)) {
			idxd->cmd_status = IDXD_SCMD_WQ_NO_SVM;
			dev_dbg(dev, "PASID not enabled and shared wq.\n");
			goto err;
		}
		/*
		 * Shared wq with the threshold set to 0 means the user
		 * did not set the threshold or transitioned from a
		 * dedicated wq but did not set threshold. A value
		 * of 0 would effectively disable the shared wq. The
		 * driver does not allow a value of 0 to be set for
		 * threshold via sysfs.
		 */
		if (wq->threshold == 0) {
			idxd->cmd_status = IDXD_SCMD_WQ_NO_THRESH;
			dev_dbg(dev, "Shared wq and threshold 0.\n");
			goto err;
		}
	}

	rc = 0;
	spin_lock(&idxd->dev_lock);
	if (test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		rc = idxd_device_config(idxd);
	spin_unlock(&idxd->dev_lock);
	if (rc < 0) {
		dev_dbg(dev, "Writing wq %d config failed: %d\n", wq->id, rc);
		goto err;
	}

	rc = idxd_wq_enable(wq);
	if (rc < 0) {
		dev_dbg(dev, "wq %d enabling failed: %d\n", wq->id, rc);
		goto err;
	}

	rc = idxd_wq_map_portal(wq);
	if (rc < 0) {
		idxd->cmd_status = IDXD_SCMD_WQ_PORTAL_ERR;
		dev_dbg(dev, "wq %d portal mapping failed: %d\n", wq->id, rc);
		goto err_map_portal;
	}

	wq->client_count = 0;
	return 0;

err_map_portal:
	rc = idxd_wq_disable(wq, false);
	if (rc < 0)
		dev_dbg(dev, "wq %s disable failed\n", dev_name(wq_confdev(wq)));
err:
	return rc;
}

int drv_enable_wq(struct idxd_wq *wq)
{
	int rc;

	mutex_lock(&wq->wq_lock);
	rc = __drv_enable_wq(wq);
	mutex_unlock(&wq->wq_lock);
	return rc;
}

void __drv_disable_wq(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;

	lockdep_assert_held(&wq->wq_lock);

	if (idxd_wq_refcount(wq))
		dev_warn(dev, "Clients has claim on wq %d: %d\n",
			 wq->id, idxd_wq_refcount(wq));

	idxd_wq_unmap_portal(wq);

	idxd_wq_drain(wq);
	idxd_wq_reset(wq);

	wq->client_count = 0;
}

void drv_disable_wq(struct idxd_wq *wq)
{
	mutex_lock(&wq->wq_lock);
	__drv_disable_wq(wq);
	mutex_unlock(&wq->wq_lock);
}

int idxd_device_drv_probe(struct idxd_dev *idxd_dev)
{
	struct idxd_device *idxd = idxd_dev_to_idxd(idxd_dev);
	int rc = 0;

	/*
	 * Device should be in disabled state for the idxd_drv to load. If it's in
	 * enabled state, then the device was altered outside of driver's control.
	 * If the state is in halted state, then we don't want to proceed.
	 */
	if (idxd->state != IDXD_DEV_DISABLED) {
		idxd->cmd_status = IDXD_SCMD_DEV_ENABLED;
		return -ENXIO;
	}

	/* Device configuration */
	spin_lock(&idxd->dev_lock);
	if (test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		rc = idxd_device_config(idxd);
	spin_unlock(&idxd->dev_lock);
	if (rc < 0)
		return -ENXIO;

	/* Start device */
	rc = idxd_device_enable(idxd);
	if (rc < 0)
		return rc;

	/* Setup DMA device without channels */
	rc = idxd_register_dma_device(idxd);
	if (rc < 0) {
		idxd_device_disable(idxd);
		idxd->cmd_status = IDXD_SCMD_DEV_DMA_ERR;
		return rc;
	}

	idxd->cmd_status = 0;
	return 0;
}

void idxd_device_drv_remove(struct idxd_dev *idxd_dev)
{
	struct device *dev = &idxd_dev->conf_dev;
	struct idxd_device *idxd = idxd_dev_to_idxd(idxd_dev);
	int i;

	for (i = 0; i < idxd->max_wqs; i++) {
		struct idxd_wq *wq = idxd->wqs[i];
		struct device *wq_dev = wq_confdev(wq);

		if (wq->state == IDXD_WQ_DISABLED)
			continue;
		dev_warn(dev, "Active wq %d on disable %s.\n", i, dev_name(wq_dev));
		device_release_driver(wq_dev);
	}

	idxd_unregister_dma_device(idxd);
	idxd_device_disable(idxd);
	if (test_bit(IDXD_FLAG_CONFIGURABLE, &idxd->flags))
		idxd_device_reset(idxd);
}

static enum idxd_dev_type dev_types[] = {
	IDXD_DEV_DSA,
	IDXD_DEV_IAX,
	IDXD_DEV_NONE,
};

struct idxd_device_driver idxd_drv = {
	.type = dev_types,
	.probe = idxd_device_drv_probe,
	.remove = idxd_device_drv_remove,
	.name = "idxd",
};
EXPORT_SYMBOL_GPL(idxd_drv);
