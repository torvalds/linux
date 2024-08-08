// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* -------------------------------------------------------------------------
 * Includes
 * -------------------------------------------------------------------------
 */
#include <linux/msm_dma_iommu_mapping.h>
#include <soc/qcom/subsystem_restart.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/qcom_scm.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "npu_hw_access.h"
#include "npu_common.h"
#include "npu_hw.h"

/* -------------------------------------------------------------------------
 * Functions - Register
 * -------------------------------------------------------------------------
 */
static uint32_t npu_reg_read(void __iomem *base, size_t size, uint32_t off)
{
	if (!base) {
		pr_err("NULL base address\n");
		return 0;
	}

	if ((off % 4) != 0) {
		pr_err("offset %x is not aligned\n", off);
		return 0;
	}

	if (off >= size) {
		pr_err("offset exceeds io region %x:%x\n", off, size);
		return 0;
	}

	return readl_relaxed(base + off);
}

static void npu_reg_write(void __iomem *base, size_t size, uint32_t off,
	uint32_t val)
{
	if (!base) {
		pr_err("NULL base address\n");
		return;
	}

	if ((off % 4) != 0) {
		pr_err("offset %x is not aligned\n", off);
		return;
	}

	if (off >= size) {
		pr_err("offset exceeds io region %x:%x\n", off, size);
		return;
	}

	writel_relaxed(val, base + off);
	__iowmb();
}

uint32_t npu_core_reg_read(struct npu_device *npu_dev, uint32_t off)
{
	return npu_reg_read(npu_dev->core_io.base, npu_dev->core_io.size, off);
}

void npu_core_reg_write(struct npu_device *npu_dev, uint32_t off, uint32_t val)
{
	npu_reg_write(npu_dev->core_io.base, npu_dev->core_io.size,
		off, val);
}

uint32_t npu_bwmon_reg_read(struct npu_device *npu_dev, uint32_t off)
{
	return npu_reg_read(npu_dev->bwmon_io.base, npu_dev->bwmon_io.size,
		off);
}

void npu_bwmon_reg_write(struct npu_device *npu_dev, uint32_t off,
	uint32_t val)
{
	npu_reg_write(npu_dev->bwmon_io.base, npu_dev->bwmon_io.size,
		off, val);
}

uint32_t npu_qfprom_reg_read(struct npu_device *npu_dev, uint32_t off)
{
	return npu_reg_read(npu_dev->qfprom_io.base,
		npu_dev->qfprom_io.size, off);
}

/* -------------------------------------------------------------------------
 * Functions - Memory
 * -------------------------------------------------------------------------
 */
void npu_mem_write(struct npu_device *npu_dev, void *dst, void *src,
	uint32_t size)
{
	size_t dst_off = (size_t)dst;
	uint32_t *src_ptr32 = (uint32_t *)src;
	uint8_t *src_ptr8 = NULL;
	uint32_t i = 0;
	uint32_t num = 0;

	if (dst_off >= npu_dev->tcm_io.size ||
		(npu_dev->tcm_io.size - dst_off) < size) {
		pr_err("memory write exceeds io region %x:%x:%x\n",
			dst_off, size, npu_dev->tcm_io.size);
		return;
	}

	num = size/4;
	for (i = 0; i < num; i++) {
		writel_relaxed(src_ptr32[i], npu_dev->tcm_io.base + dst_off);
		dst_off += 4;
	}

	if (size%4 != 0) {
		src_ptr8 = (uint8_t *)((size_t)src + (num*4));
		num = size%4;
		for (i = 0; i < num; i++) {
			writeb_relaxed(src_ptr8[i], npu_dev->tcm_io.base +
				dst_off);
			dst_off += 1;
		}
	}

	__iowmb();
}

int32_t npu_mem_read(struct npu_device *npu_dev, void *src, void *dst,
	uint32_t size)
{
	size_t src_off = (size_t)src;
	uint32_t *out32 = (uint32_t *)dst;
	uint8_t *out8 = NULL;
	uint32_t i = 0;
	uint32_t num = 0;

	if (src_off >= npu_dev->tcm_io.size ||
		(npu_dev->tcm_io.size - src_off) < size) {
		pr_err("memory read exceeds io region %x:%x:%x\n",
			src_off, size, npu_dev->tcm_io.size);
		return 0;
	}

	num = size/4;
	for (i = 0; i < num; i++) {
		out32[i] = readl_relaxed(npu_dev->tcm_io.base + src_off);
		src_off += 4;
	}

	if (size%4 != 0) {
		out8 = (uint8_t *)((size_t)dst + (num*4));
		num = size%4;
		for (i = 0; i < num; i++) {
			out8[i] = readb_relaxed(npu_dev->tcm_io.base + src_off);
			src_off += 1;
		}
	}
	return 0;
}

void *npu_ipc_addr(void)
{
	return (void *)(IPC_MEM_OFFSET_FROM_SSTCM);
}

/* -------------------------------------------------------------------------
 * Functions - Interrupt
 * -------------------------------------------------------------------------
 */
void npu_interrupt_ack(struct npu_device *npu_dev, uint32_t intr_num)
{
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	uint32_t wdg_irq_sts = 0, error_irq_sts = 0;

	/* Clear irq state */
	REGW(npu_dev, NPU_MASTERn_IPC_IRQ_OUT(0), 0x0);

	wdg_irq_sts = REGR(npu_dev, NPU_MASTERn_WDOG_IRQ_STATUS(0));
	if (wdg_irq_sts != 0) {
		pr_err("wdg irq %x\n", wdg_irq_sts);
		host_ctx->wdg_irq_sts |= wdg_irq_sts;
		host_ctx->fw_error = true;
	}

	error_irq_sts = REGR(npu_dev, NPU_MASTERn_ERROR_IRQ_STATUS(0));
	error_irq_sts &= REGR(npu_dev, NPU_MASTERn_ERROR_IRQ_ENABLE(0));
	if (error_irq_sts != 0) {
		REGW(npu_dev, NPU_MASTERn_ERROR_IRQ_CLEAR(0), error_irq_sts);
		pr_err("error irq %x\n", error_irq_sts);
		host_ctx->err_irq_sts |= error_irq_sts;
		host_ctx->fw_error = true;
	}
}

int32_t npu_interrupt_raise_m0(struct npu_device *npu_dev)
{
	/* Bit 4 is setting IRQ_SOURCE_SELECT to local
	 * and we're triggering a pulse to NPU_MASTER0_IPC_IN_IRQ0
	 */
	npu_core_reg_write(npu_dev, NPU_MASTERn_IPC_IRQ_IN_CTRL(0), 0x1
		<< NPU_MASTER0_IPC_IRQ_IN_CTRL__IRQ_SOURCE_SELECT___S | 0x1);

	return 0;
}

int32_t npu_interrupt_raise_dsp(struct npu_device *npu_dev)
{
	npu_core_reg_write(npu_dev, NPU_MASTERn_IPC_IRQ_OUT_CTRL(1), 0x8);

	return 0;
}

/* -------------------------------------------------------------------------
 * Functions - ION Memory
 * -------------------------------------------------------------------------
 */
static struct npu_ion_buf *npu_alloc_npu_ion_buffer(struct npu_client
	*client, int buf_hdl, uint32_t size)
{
	struct npu_ion_buf *ret_val = NULL, *tmp;
	struct list_head *pos = NULL;

	mutex_lock(&client->list_lock);
	list_for_each(pos, &(client->mapped_buffer_list)) {
		tmp = list_entry(pos, struct npu_ion_buf, list);
		if (tmp->fd == buf_hdl) {
			ret_val = tmp;
			break;
		}
	}

	if (ret_val) {
		/* mapped already, treat as invalid request */
		pr_err("ion buf has been mapped\n");
		ret_val = NULL;
	} else {
		ret_val = kzalloc(sizeof(*ret_val), GFP_KERNEL);
		if (ret_val) {
			ret_val->fd = buf_hdl;
			ret_val->size = size;
			ret_val->iova = 0;
			list_add(&(ret_val->list),
				&(client->mapped_buffer_list));
		}
	}
	mutex_unlock(&client->list_lock);

	return ret_val;
}

static struct npu_ion_buf *npu_get_npu_ion_buffer(struct npu_client
	*client, int buf_hdl)
{
	struct list_head *pos = NULL;
	struct npu_ion_buf *ret_val = NULL, *tmp;

	mutex_lock(&client->list_lock);
	list_for_each(pos, &(client->mapped_buffer_list)) {
		tmp = list_entry(pos, struct npu_ion_buf, list);
		if (tmp->fd == buf_hdl) {
			ret_val = tmp;
			break;
		}
	}
	mutex_unlock(&client->list_lock);

	return ret_val;
}

static void npu_free_npu_ion_buffer(struct npu_client
	*client, int buf_hdl)
{
	struct list_head *pos = NULL;
	struct npu_ion_buf *npu_ion_buf = NULL;

	mutex_lock(&client->list_lock);
	list_for_each(pos, &(client->mapped_buffer_list)) {
		npu_ion_buf = list_entry(pos, struct npu_ion_buf, list);
		if (npu_ion_buf->fd == buf_hdl) {
			list_del(&npu_ion_buf->list);
			kfree(npu_ion_buf);
			break;
		}
	}
	mutex_unlock(&client->list_lock);
}

int npu_mem_map(struct npu_client *client, int buf_hdl, uint32_t size,
	uint64_t *addr)
{
	MODULE_IMPORT_NS(DMA_BUF);
	int ret = 0;
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_ion_buf *ion_buf = NULL;
	struct npu_smmu_ctx *smmu_ctx = &npu_dev->smmu_ctx;

	if (buf_hdl == 0)
		return -EINVAL;

	ion_buf = npu_alloc_npu_ion_buffer(client, buf_hdl, size);
	if (!ion_buf) {
		pr_err("%s fail to alloc npu_ion_buffer\n", __func__);
		ret = -ENOMEM;
		return ret;
	}

	smmu_ctx->attach_cnt++;

	ion_buf->dma_buf = dma_buf_get(ion_buf->fd);
	if (IS_ERR_OR_NULL(ion_buf->dma_buf)) {
		pr_err("dma_buf_get failed %d\n", ion_buf->fd);
		ret = -ENOMEM;
		ion_buf->dma_buf = NULL;
		goto map_end;
	}

	ion_buf->attachment = dma_buf_attach(ion_buf->dma_buf,
			&(npu_dev->pdev->dev));
	if (IS_ERR(ion_buf->attachment)) {
		ret = -ENOMEM;
		ion_buf->attachment = NULL;
		goto map_end;
	}

	ion_buf->attachment->dma_map_attrs = DMA_ATTR_IOMMU_USE_UPSTREAM_HINT;

	ion_buf->table = dma_buf_map_attachment(ion_buf->attachment,
			DMA_BIDIRECTIONAL);
	if (IS_ERR(ion_buf->table)) {
		pr_err("npu dma_buf_map_attachment failed\n");
		ret = -ENOMEM;
		ion_buf->table = NULL;
		goto map_end;
	}

	ion_buf->iova = ion_buf->table->sgl->dma_address;
	ion_buf->size = ion_buf->dma_buf->size;
	*addr = ion_buf->iova;
	pr_debug("mapped mem addr:0x%llx size:0x%x\n", ion_buf->iova,
		ion_buf->size);
map_end:
	if (ret)
		npu_mem_unmap(client, buf_hdl, 0);

	return ret;
}

void npu_mem_invalidate(struct npu_client *client, int buf_hdl)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_ion_buf *ion_buf = npu_get_npu_ion_buffer(client,
		buf_hdl);

	if (!ion_buf)
		pr_err("%s can't find ion buf\n", __func__);
	else
		dma_sync_sg_for_cpu(&(npu_dev->pdev->dev), ion_buf->table->sgl,
			ion_buf->table->nents, DMA_BIDIRECTIONAL);
}

bool npu_mem_verify_addr(struct npu_client *client, uint64_t addr)
{
	struct npu_ion_buf *ion_buf = NULL;
	struct list_head *pos = NULL;
	bool valid = false;

	mutex_lock(&client->list_lock);
	list_for_each(pos, &(client->mapped_buffer_list)) {
		ion_buf = list_entry(pos, struct npu_ion_buf, list);
		if (ion_buf->iova == addr) {
			valid = true;
			break;
		}
	}
	mutex_unlock(&client->list_lock);

	return valid;
}

void npu_mem_unmap(struct npu_client *client, int buf_hdl,  uint64_t addr)
{
	MODULE_IMPORT_NS(DMA_BUF);
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_ion_buf *ion_buf = NULL;

	/* clear entry and retrieve the corresponding buffer */
	ion_buf = npu_get_npu_ion_buffer(client, buf_hdl);
	if (!ion_buf) {
		pr_err("%s could not find buffer\n", __func__);
		return;
	}

	if (ion_buf->iova != addr)
		pr_warn("unmap address %llu doesn't match %llu\n", addr,
			ion_buf->iova);

	if (ion_buf->table)
		dma_buf_unmap_attachment(ion_buf->attachment, ion_buf->table,
			DMA_BIDIRECTIONAL);
	if (ion_buf->dma_buf && ion_buf->attachment)
		dma_buf_detach(ion_buf->dma_buf, ion_buf->attachment);
	if (ion_buf->dma_buf)
		dma_buf_put(ion_buf->dma_buf);
	npu_dev->smmu_ctx.attach_cnt--;

	pr_debug("unmapped mem addr:0x%llx size:0x%x\n", ion_buf->iova,
		ion_buf->size);
	npu_free_npu_ion_buffer(client, buf_hdl);
}

/* -------------------------------------------------------------------------
 * Functions - Features
 * -------------------------------------------------------------------------
 */
uint8_t npu_hw_clk_gating_enabled(void)
{
	return 1;
}

uint8_t npu_hw_log_enabled(void)
{
	return 1;
}

/* -------------------------------------------------------------------------
 * Functions - Subsystem/PIL
 * -------------------------------------------------------------------------
 */
#define NPU_PAS_ID (23)

int npu_subsystem_get(struct npu_device *npu_dev, const char *fw_name)
{
	struct device *dev = npu_dev->device;
	const struct firmware *firmware_p;
	ssize_t fw_size;
	/* load firmware */
	int ret = request_firmware(&firmware_p, fw_name, dev);

	if (ret < 0) {
		pr_err("request_firmware %s failed: %d\n", fw_name, ret);
		return ret;
	}
	fw_size = qcom_mdt_get_size(firmware_p);
	if (fw_size < 0 || fw_size > npu_dev->fw_io.mem_size) {
		pr_err("npu fw size invalid, %lld\n", fw_size);
		return -EINVAL;
	}
	/* load the ELF segments to memory */
	ret = qcom_mdt_load(dev, firmware_p, fw_name, NPU_PAS_ID,
				    npu_dev->fw_io.mem_region, npu_dev->fw_io.mem_phys,
				    npu_dev->fw_io.mem_size, &npu_dev->fw_io.mem_reloc);
	release_firmware(firmware_p);
	if (ret) {
		pr_err("qcom_mdt_load failure, %d\n", ret);
		return ret;
	}
	ret = qcom_scm_pas_auth_and_reset(NPU_PAS_ID);
	if (ret) {
		pr_err("failed to authenticate image and release reset\n");
		return -2;
	}
	pr_debug("done pas auth\n");
	return 0;
}

void npu_subsystem_put(struct npu_device *npu_dev)
{
	int ret = qcom_scm_pas_shutdown(NPU_PAS_ID);

	if (ret)
		pr_err("failed to shutdown: %d\n", ret);

}
