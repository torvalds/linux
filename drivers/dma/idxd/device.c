// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2019 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/dmaengine.h>
#include <uapi/linux/idxd.h>
#include "../dmaengine.h"
#include "idxd.h"
#include "registers.h"

static int idxd_cmd_wait(struct idxd_device *idxd, u32 *status, int timeout);
static int idxd_cmd_send(struct idxd_device *idxd, int cmd_code, u32 operand);

/* Interrupt control bits */
int idxd_mask_msix_vector(struct idxd_device *idxd, int vec_id)
{
	struct pci_dev *pdev = idxd->pdev;
	int msixcnt = pci_msix_vec_count(pdev);
	union msix_perm perm;
	u32 offset;

	if (vec_id < 0 || vec_id >= msixcnt)
		return -EINVAL;

	offset = idxd->msix_perm_offset + vec_id * 8;
	perm.bits = ioread32(idxd->reg_base + offset);
	perm.ignore = 1;
	iowrite32(perm.bits, idxd->reg_base + offset);

	return 0;
}

void idxd_mask_msix_vectors(struct idxd_device *idxd)
{
	struct pci_dev *pdev = idxd->pdev;
	int msixcnt = pci_msix_vec_count(pdev);
	int i, rc;

	for (i = 0; i < msixcnt; i++) {
		rc = idxd_mask_msix_vector(idxd, i);
		if (rc < 0)
			dev_warn(&pdev->dev,
				 "Failed disabling msix vec %d\n", i);
	}
}

int idxd_unmask_msix_vector(struct idxd_device *idxd, int vec_id)
{
	struct pci_dev *pdev = idxd->pdev;
	int msixcnt = pci_msix_vec_count(pdev);
	union msix_perm perm;
	u32 offset;

	if (vec_id < 0 || vec_id >= msixcnt)
		return -EINVAL;

	offset = idxd->msix_perm_offset + vec_id * 8;
	perm.bits = ioread32(idxd->reg_base + offset);
	perm.ignore = 0;
	iowrite32(perm.bits, idxd->reg_base + offset);

	/*
	 * A readback from the device ensures that any previously generated
	 * completion record writes are visible to software based on PCI
	 * ordering rules.
	 */
	perm.bits = ioread32(idxd->reg_base + offset);

	return 0;
}

void idxd_unmask_error_interrupts(struct idxd_device *idxd)
{
	union genctrl_reg genctrl;

	genctrl.bits = ioread32(idxd->reg_base + IDXD_GENCTRL_OFFSET);
	genctrl.softerr_int_en = 1;
	iowrite32(genctrl.bits, idxd->reg_base + IDXD_GENCTRL_OFFSET);
}

void idxd_mask_error_interrupts(struct idxd_device *idxd)
{
	union genctrl_reg genctrl;

	genctrl.bits = ioread32(idxd->reg_base + IDXD_GENCTRL_OFFSET);
	genctrl.softerr_int_en = 0;
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
	struct idxd_group *group = wq->group;
	struct device *dev = &idxd->pdev->dev;
	int rc, num_descs, i;

	if (wq->type != IDXD_WQT_KERNEL)
		return 0;

	num_descs = wq->size +
		idxd->hw.gen_cap.max_descs_per_engine * group->num_engines;
	wq->num_descs = num_descs;

	rc = alloc_hw_descs(wq, num_descs);
	if (rc < 0)
		return rc;

	wq->compls_size = num_descs * sizeof(struct dsa_completion_record);
	wq->compls = dma_alloc_coherent(dev, wq->compls_size,
					&wq->compls_addr, GFP_KERNEL);
	if (!wq->compls) {
		rc = -ENOMEM;
		goto fail_alloc_compls;
	}

	rc = alloc_descs(wq, num_descs);
	if (rc < 0)
		goto fail_alloc_descs;

	rc = sbitmap_init_node(&wq->sbmap, num_descs, -1, GFP_KERNEL,
			       dev_to_node(dev));
	if (rc < 0)
		goto fail_sbitmap_init;

	for (i = 0; i < num_descs; i++) {
		struct idxd_desc *desc = wq->descs[i];

		desc->hw = wq->hw_descs[i];
		desc->completion = &wq->compls[i];
		desc->compl_dma  = wq->compls_addr +
			sizeof(struct dsa_completion_record) * i;
		desc->id = i;
		desc->wq = wq;

		dma_async_tx_descriptor_init(&desc->txd, &wq->dma_chan);
		desc->txd.tx_submit = idxd_dma_tx_submit;
	}

	return 0;

 fail_sbitmap_init:
	free_descs(wq);
 fail_alloc_descs:
	dma_free_coherent(dev, wq->compls_size, wq->compls, wq->compls_addr);
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
	dma_free_coherent(dev, wq->compls_size, wq->compls, wq->compls_addr);
	sbitmap_free(&wq->sbmap);
}

int idxd_wq_enable(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	u32 status;
	int rc;

	lockdep_assert_held(&idxd->dev_lock);

	if (wq->state == IDXD_WQ_ENABLED) {
		dev_dbg(dev, "WQ %d already enabled\n", wq->id);
		return -ENXIO;
	}

	rc = idxd_cmd_send(idxd, IDXD_CMD_ENABLE_WQ, wq->id);
	if (rc < 0)
		return rc;
	rc = idxd_cmd_wait(idxd, &status, IDXD_REG_TIMEOUT);
	if (rc < 0)
		return rc;

	if (status != IDXD_CMDSTS_SUCCESS &&
	    status != IDXD_CMDSTS_ERR_WQ_ENABLED) {
		dev_dbg(dev, "WQ enable failed: %#x\n", status);
		return -ENXIO;
	}

	wq->state = IDXD_WQ_ENABLED;
	dev_dbg(dev, "WQ %d enabled\n", wq->id);
	return 0;
}

int idxd_wq_disable(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	u32 status, operand;
	int rc;

	lockdep_assert_held(&idxd->dev_lock);
	dev_dbg(dev, "Disabling WQ %d\n", wq->id);

	if (wq->state != IDXD_WQ_ENABLED) {
		dev_dbg(dev, "WQ %d in wrong state: %d\n", wq->id, wq->state);
		return 0;
	}

	operand = BIT(wq->id % 16) | ((wq->id / 16) << 16);
	rc = idxd_cmd_send(idxd, IDXD_CMD_DISABLE_WQ, operand);
	if (rc < 0)
		return rc;
	rc = idxd_cmd_wait(idxd, &status, IDXD_REG_TIMEOUT);
	if (rc < 0)
		return rc;

	if (status != IDXD_CMDSTS_SUCCESS) {
		dev_dbg(dev, "WQ disable failed: %#x\n", status);
		return -ENXIO;
	}

	wq->state = IDXD_WQ_DISABLED;
	dev_dbg(dev, "WQ %d disabled\n", wq->id);
	return 0;
}

int idxd_wq_map_portal(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct pci_dev *pdev = idxd->pdev;
	struct device *dev = &pdev->dev;
	resource_size_t start;

	start = pci_resource_start(pdev, IDXD_WQ_BAR);
	start = start + wq->id * IDXD_PORTAL_SIZE;

	wq->dportal = devm_ioremap(dev, start, IDXD_PORTAL_SIZE);
	if (!wq->dportal)
		return -ENOMEM;
	dev_dbg(dev, "wq %d portal mapped at %p\n", wq->id, wq->dportal);

	return 0;
}

void idxd_wq_unmap_portal(struct idxd_wq *wq)
{
	struct device *dev = &wq->idxd->pdev->dev;

	devm_iounmap(dev, wq->dportal);
}

void idxd_wq_disable_cleanup(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	int i, wq_offset;

	lockdep_assert_held(&idxd->dev_lock);
	memset(&wq->wqcfg, 0, sizeof(wq->wqcfg));
	wq->type = IDXD_WQT_NONE;
	wq->size = 0;
	wq->group = NULL;
	wq->threshold = 0;
	wq->priority = 0;
	clear_bit(WQ_FLAG_DEDICATED, &wq->flags);
	memset(wq->name, 0, WQ_NAME_SIZE);

	for (i = 0; i < 8; i++) {
		wq_offset = idxd->wqcfg_offset + wq->id * 32 + i * sizeof(u32);
		iowrite32(0, idxd->reg_base + wq_offset);
		dev_dbg(dev, "WQ[%d][%d][%#x]: %#x\n",
			wq->id, i, wq_offset,
			ioread32(idxd->reg_base + wq_offset));
	}
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

static int idxd_cmd_wait(struct idxd_device *idxd, u32 *status, int timeout)
{
	u32 sts, to = timeout;

	lockdep_assert_held(&idxd->dev_lock);
	sts = ioread32(idxd->reg_base + IDXD_CMDSTS_OFFSET);
	while (sts & IDXD_CMDSTS_ACTIVE && --to) {
		cpu_relax();
		sts = ioread32(idxd->reg_base + IDXD_CMDSTS_OFFSET);
	}

	if (to == 0 && sts & IDXD_CMDSTS_ACTIVE) {
		dev_warn(&idxd->pdev->dev, "%s timed out!\n", __func__);
		*status = 0;
		return -EBUSY;
	}

	*status = sts;
	return 0;
}

static int idxd_cmd_send(struct idxd_device *idxd, int cmd_code, u32 operand)
{
	union idxd_command_reg cmd;
	int rc;
	u32 status;

	lockdep_assert_held(&idxd->dev_lock);
	rc = idxd_cmd_wait(idxd, &status, IDXD_REG_TIMEOUT);
	if (rc < 0)
		return rc;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = cmd_code;
	cmd.operand = operand;
	dev_dbg(&idxd->pdev->dev, "%s: sending cmd: %#x op: %#x\n",
		__func__, cmd_code, operand);
	iowrite32(cmd.bits, idxd->reg_base + IDXD_CMD_OFFSET);

	return 0;
}

int idxd_device_enable(struct idxd_device *idxd)
{
	struct device *dev = &idxd->pdev->dev;
	int rc;
	u32 status;

	lockdep_assert_held(&idxd->dev_lock);
	if (idxd_is_enabled(idxd)) {
		dev_dbg(dev, "Device already enabled\n");
		return -ENXIO;
	}

	rc = idxd_cmd_send(idxd, IDXD_CMD_ENABLE_DEVICE, 0);
	if (rc < 0)
		return rc;
	rc = idxd_cmd_wait(idxd, &status, IDXD_REG_TIMEOUT);
	if (rc < 0)
		return rc;

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
	int rc;
	u32 status;

	lockdep_assert_held(&idxd->dev_lock);
	if (!idxd_is_enabled(idxd)) {
		dev_dbg(dev, "Device is not enabled\n");
		return 0;
	}

	rc = idxd_cmd_send(idxd, IDXD_CMD_DISABLE_DEVICE, 0);
	if (rc < 0)
		return rc;
	rc = idxd_cmd_wait(idxd, &status, IDXD_REG_TIMEOUT);
	if (rc < 0)
		return rc;

	/* If the command is successful or if the device was disabled */
	if (status != IDXD_CMDSTS_SUCCESS &&
	    !(status & IDXD_CMDSTS_ERR_DIS_DEV_EN)) {
		dev_dbg(dev, "%s: err_code: %#x\n", __func__, status);
		rc = -ENXIO;
		return rc;
	}

	idxd->state = IDXD_DEV_CONF_READY;
	return 0;
}

int __idxd_device_reset(struct idxd_device *idxd)
{
	u32 status;
	int rc;

	rc = idxd_cmd_send(idxd, IDXD_CMD_RESET_DEVICE, 0);
	if (rc < 0)
		return rc;
	rc = idxd_cmd_wait(idxd, &status, IDXD_REG_TIMEOUT);
	if (rc < 0)
		return rc;

	return 0;
}

int idxd_device_reset(struct idxd_device *idxd)
{
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&idxd->dev_lock, flags);
	rc = __idxd_device_reset(idxd);
	spin_unlock_irqrestore(&idxd->dev_lock, flags);
	return rc;
}

/* Device configuration bits */
static void idxd_group_config_write(struct idxd_group *group)
{
	struct idxd_device *idxd = group->idxd;
	struct device *dev = &idxd->pdev->dev;
	int i;
	u32 grpcfg_offset;

	dev_dbg(dev, "Writing group %d cfg registers\n", group->id);

	/* setup GRPWQCFG */
	for (i = 0; i < 4; i++) {
		grpcfg_offset = idxd->grpcfg_offset +
			group->id * 64 + i * sizeof(u64);
		iowrite64(group->grpcfg.wqs[i],
			  idxd->reg_base + grpcfg_offset);
		dev_dbg(dev, "GRPCFG wq[%d:%d: %#x]: %#llx\n",
			group->id, i, grpcfg_offset,
			ioread64(idxd->reg_base + grpcfg_offset));
	}

	/* setup GRPENGCFG */
	grpcfg_offset = idxd->grpcfg_offset + group->id * 64 + 32;
	iowrite64(group->grpcfg.engines, idxd->reg_base + grpcfg_offset);
	dev_dbg(dev, "GRPCFG engs[%d: %#x]: %#llx\n", group->id,
		grpcfg_offset, ioread64(idxd->reg_base + grpcfg_offset));

	/* setup GRPFLAGS */
	grpcfg_offset = idxd->grpcfg_offset + group->id * 64 + 40;
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
	if (idxd->token_limit) {
		reg.bits = ioread32(idxd->reg_base + IDXD_GENCFG_OFFSET);
		reg.token_limit = idxd->token_limit;
		iowrite32(reg.bits, idxd->reg_base + IDXD_GENCFG_OFFSET);
	}

	dev_dbg(dev, "GENCFG(%#x): %#x\n", IDXD_GENCFG_OFFSET,
		ioread32(idxd->reg_base + IDXD_GENCFG_OFFSET));

	for (i = 0; i < idxd->max_groups; i++) {
		struct idxd_group *group = &idxd->groups[i];

		idxd_group_config_write(group);
	}

	return 0;
}

static int idxd_wq_config_write(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct device *dev = &idxd->pdev->dev;
	u32 wq_offset;
	int i;

	if (!wq->group)
		return 0;

	memset(&wq->wqcfg, 0, sizeof(union wqcfg));

	/* byte 0-3 */
	wq->wqcfg.wq_size = wq->size;

	if (wq->size == 0) {
		dev_warn(dev, "Incorrect work queue size: 0\n");
		return -EINVAL;
	}

	/* bytes 4-7 */
	wq->wqcfg.wq_thresh = wq->threshold;

	/* byte 8-11 */
	wq->wqcfg.priv = !!(wq->type == IDXD_WQT_KERNEL);
	wq->wqcfg.mode = 1;

	wq->wqcfg.priority = wq->priority;

	/* bytes 12-15 */
	wq->wqcfg.max_xfer_shift = idxd->hw.gen_cap.max_xfer_shift;
	wq->wqcfg.max_batch_shift = idxd->hw.gen_cap.max_batch_shift;

	dev_dbg(dev, "WQ %d CFGs\n", wq->id);
	for (i = 0; i < 8; i++) {
		wq_offset = idxd->wqcfg_offset + wq->id * 32 + i * sizeof(u32);
		iowrite32(wq->wqcfg.bits[i], idxd->reg_base + wq_offset);
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
		struct idxd_wq *wq = &idxd->wqs[i];

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
		struct idxd_group *group = &idxd->groups[i];

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
		group = &idxd->groups[i];
		group->grpcfg.engines = 0;
	}

	for (i = 0; i < idxd->max_engines; i++) {
		eng = &idxd->engines[i];
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
		group = &idxd->groups[i];
		for (j = 0; j < 4; j++)
			group->grpcfg.wqs[j] = 0;
	}

	for (i = 0; i < idxd->max_wqs; i++) {
		wq = &idxd->wqs[i];
		group = wq->group;

		if (!wq->group)
			continue;
		if (!wq->size)
			continue;

		if (!wq_dedicated(wq)) {
			dev_warn(dev, "No shared workqueue support.\n");
			return -EINVAL;
		}

		group->grpcfg.wqs[wq->id / 64] |= BIT(wq->id % 64);
		configured++;
	}

	if (configured == 0)
		return -EINVAL;

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
