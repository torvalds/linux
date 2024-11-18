// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#include <drm/drm_device.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/pci.h>

#include "aie2_msg_priv.h"
#include "aie2_pci.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_pci_drv.h"

/*
 * The management mailbox channel is allocated by firmware.
 * The related register and ring buffer information is on SRAM BAR.
 * This struct is the register layout.
 */
struct mgmt_mbox_chann_info {
	u32	x2i_tail;
	u32	x2i_head;
	u32	x2i_buf;
	u32	x2i_buf_sz;
	u32	i2x_tail;
	u32	i2x_head;
	u32	i2x_buf;
	u32	i2x_buf_sz;
};

static void aie2_dump_chann_info_debug(struct amdxdna_dev_hdl *ndev)
{
	struct amdxdna_dev *xdna = ndev->xdna;

	XDNA_DBG(xdna, "i2x tail    0x%x", ndev->mgmt_i2x.mb_tail_ptr_reg);
	XDNA_DBG(xdna, "i2x head    0x%x", ndev->mgmt_i2x.mb_head_ptr_reg);
	XDNA_DBG(xdna, "i2x ringbuf 0x%x", ndev->mgmt_i2x.rb_start_addr);
	XDNA_DBG(xdna, "i2x rsize   0x%x", ndev->mgmt_i2x.rb_size);
	XDNA_DBG(xdna, "x2i tail    0x%x", ndev->mgmt_x2i.mb_tail_ptr_reg);
	XDNA_DBG(xdna, "x2i head    0x%x", ndev->mgmt_x2i.mb_head_ptr_reg);
	XDNA_DBG(xdna, "x2i ringbuf 0x%x", ndev->mgmt_x2i.rb_start_addr);
	XDNA_DBG(xdna, "x2i rsize   0x%x", ndev->mgmt_x2i.rb_size);
	XDNA_DBG(xdna, "x2i chann index 0x%x", ndev->mgmt_chan_idx);
}

static int aie2_get_mgmt_chann_info(struct amdxdna_dev_hdl *ndev)
{
	struct mgmt_mbox_chann_info info_regs;
	struct xdna_mailbox_chann_res *i2x;
	struct xdna_mailbox_chann_res *x2i;
	u32 addr, off;
	u32 *reg;
	int ret;
	int i;

	/*
	 * Once firmware is alive, it will write management channel
	 * information in SRAM BAR and write the address of that information
	 * at FW_ALIVE_OFF offset in SRMA BAR.
	 *
	 * Read a non-zero value from FW_ALIVE_OFF implies that firmware
	 * is alive.
	 */
	ret = readx_poll_timeout(readl, SRAM_GET_ADDR(ndev, FW_ALIVE_OFF),
				 addr, addr, AIE2_INTERVAL, AIE2_TIMEOUT);
	if (ret || !addr)
		return -ETIME;

	off = AIE2_SRAM_OFF(ndev, addr);
	reg = (u32 *)&info_regs;
	for (i = 0; i < sizeof(info_regs) / sizeof(u32); i++)
		reg[i] = readl(ndev->sram_base + off + i * sizeof(u32));

	i2x = &ndev->mgmt_i2x;
	x2i = &ndev->mgmt_x2i;

	i2x->mb_head_ptr_reg = AIE2_MBOX_OFF(ndev, info_regs.i2x_head);
	i2x->mb_tail_ptr_reg = AIE2_MBOX_OFF(ndev, info_regs.i2x_tail);
	i2x->rb_start_addr   = AIE2_SRAM_OFF(ndev, info_regs.i2x_buf);
	i2x->rb_size         = info_regs.i2x_buf_sz;

	x2i->mb_head_ptr_reg = AIE2_MBOX_OFF(ndev, info_regs.x2i_head);
	x2i->mb_tail_ptr_reg = AIE2_MBOX_OFF(ndev, info_regs.x2i_tail);
	x2i->rb_start_addr   = AIE2_SRAM_OFF(ndev, info_regs.x2i_buf);
	x2i->rb_size         = info_regs.x2i_buf_sz;
	ndev->mgmt_chan_idx  = CHANN_INDEX(ndev, x2i->rb_start_addr);

	aie2_dump_chann_info_debug(ndev);

	/* Must clear address at FW_ALIVE_OFF */
	writel(0, SRAM_GET_ADDR(ndev, FW_ALIVE_OFF));

	return 0;
}

static int aie2_runtime_cfg(struct amdxdna_dev_hdl *ndev)
{
	const struct rt_config *cfg = &ndev->priv->rt_config;
	u64 value;
	int ret;

	ret = aie2_set_runtime_cfg(ndev, cfg->type, cfg->value);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Set runtime type %d value %d failed",
			 cfg->type, cfg->value);
		return ret;
	}

	ret = aie2_get_runtime_cfg(ndev, cfg->type, &value);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Get runtime cfg failed");
		return ret;
	}

	if (value != cfg->value)
		return -EINVAL;

	return 0;
}

static int aie2_xdna_reset(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie2_suspend_fw(ndev);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Suspend firmware failed");
		return ret;
	}

	ret = aie2_resume_fw(ndev);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Resume firmware failed");
		return ret;
	}

	return 0;
}

static int aie2_mgmt_fw_init(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie2_check_protocol_version(ndev);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Check header hash failed");
		return ret;
	}

	ret = aie2_runtime_cfg(ndev);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Runtime config failed");
		return ret;
	}

	ret = aie2_assign_mgmt_pasid(ndev, 0);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Can not assign PASID");
		return ret;
	}

	ret = aie2_xdna_reset(ndev);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Reset firmware failed");
		return ret;
	}

	return 0;
}

static int aie2_mgmt_fw_query(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie2_query_firmware_version(ndev, &ndev->xdna->fw_ver);
	if (ret) {
		XDNA_ERR(ndev->xdna, "query firmware version failed");
		return ret;
	}

	ret = aie2_query_aie_version(ndev, &ndev->version);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Query AIE version failed");
		return ret;
	}

	ret = aie2_query_aie_metadata(ndev, &ndev->metadata);
	if (ret) {
		XDNA_ERR(ndev->xdna, "Query AIE metadata failed");
		return ret;
	}

	return 0;
}

static void aie2_mgmt_fw_fini(struct amdxdna_dev_hdl *ndev)
{
	if (aie2_suspend_fw(ndev))
		XDNA_ERR(ndev->xdna, "Suspend_fw failed");
	XDNA_DBG(ndev->xdna, "Firmware suspended");
}

static void aie2_hw_stop(struct amdxdna_dev *xdna)
{
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;

	aie2_mgmt_fw_fini(ndev);
	xdna_mailbox_stop_channel(ndev->mgmt_chann);
	xdna_mailbox_destroy_channel(ndev->mgmt_chann);
	aie2_psp_stop(ndev->psp_hdl);
	aie2_smu_fini(ndev);
	pci_disable_device(pdev);
}

static int aie2_hw_start(struct amdxdna_dev *xdna)
{
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	struct xdna_mailbox_res mbox_res;
	u32 xdna_mailbox_intr_reg;
	int mgmt_mb_irq, ret;

	ret = pci_enable_device(pdev);
	if (ret) {
		XDNA_ERR(xdna, "failed to enable device, ret %d", ret);
		return ret;
	}
	pci_set_master(pdev);

	ret = aie2_smu_init(ndev);
	if (ret) {
		XDNA_ERR(xdna, "failed to init smu, ret %d", ret);
		goto disable_dev;
	}

	ret = aie2_psp_start(ndev->psp_hdl);
	if (ret) {
		XDNA_ERR(xdna, "failed to start psp, ret %d", ret);
		goto fini_smu;
	}

	ret = aie2_get_mgmt_chann_info(ndev);
	if (ret) {
		XDNA_ERR(xdna, "firmware is not alive");
		goto stop_psp;
	}

	mbox_res.ringbuf_base = (u64)ndev->sram_base;
	mbox_res.ringbuf_size = pci_resource_len(pdev, xdna->dev_info->sram_bar);
	mbox_res.mbox_base = (u64)ndev->mbox_base;
	mbox_res.mbox_size = MBOX_SIZE(ndev);
	mbox_res.name = "xdna_mailbox";
	ndev->mbox = xdnam_mailbox_create(&xdna->ddev, &mbox_res);
	if (!ndev->mbox) {
		XDNA_ERR(xdna, "failed to create mailbox device");
		ret = -ENODEV;
		goto stop_psp;
	}

	mgmt_mb_irq = pci_irq_vector(pdev, ndev->mgmt_chan_idx);
	if (mgmt_mb_irq < 0) {
		ret = mgmt_mb_irq;
		XDNA_ERR(xdna, "failed to alloc irq vector, ret %d", ret);
		goto stop_psp;
	}

	xdna_mailbox_intr_reg = ndev->mgmt_i2x.mb_head_ptr_reg + 4;
	ndev->mgmt_chann = xdna_mailbox_create_channel(ndev->mbox,
						       &ndev->mgmt_x2i,
						       &ndev->mgmt_i2x,
						       xdna_mailbox_intr_reg,
						       mgmt_mb_irq);
	if (!ndev->mgmt_chann) {
		XDNA_ERR(xdna, "failed to create management mailbox channel");
		ret = -EINVAL;
		goto stop_psp;
	}

	ret = aie2_mgmt_fw_init(ndev);
	if (ret) {
		XDNA_ERR(xdna, "initial mgmt firmware failed, ret %d", ret);
		goto destroy_mgmt_chann;
	}

	return 0;

destroy_mgmt_chann:
	xdna_mailbox_stop_channel(ndev->mgmt_chann);
	xdna_mailbox_destroy_channel(ndev->mgmt_chann);
stop_psp:
	aie2_psp_stop(ndev->psp_hdl);
fini_smu:
	aie2_smu_fini(ndev);
disable_dev:
	pci_disable_device(pdev);

	return ret;
}

static int aie2_init(struct amdxdna_dev *xdna)
{
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	void __iomem *tbl[PCI_NUM_RESOURCES] = {0};
	struct amdxdna_dev_hdl *ndev;
	struct psp_config psp_conf;
	const struct firmware *fw;
	unsigned long bars = 0;
	int i, nvec, ret;

	ndev = drmm_kzalloc(&xdna->ddev, sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		return -ENOMEM;

	ndev->priv = xdna->dev_info->dev_priv;
	ndev->xdna = xdna;

	ret = request_firmware(&fw, ndev->priv->fw_path, &pdev->dev);
	if (ret) {
		XDNA_ERR(xdna, "failed to request_firmware %s, ret %d",
			 ndev->priv->fw_path, ret);
		return ret;
	}

	ret = pcim_enable_device(pdev);
	if (ret) {
		XDNA_ERR(xdna, "pcim enable device failed, ret %d", ret);
		goto release_fw;
	}

	for (i = 0; i < PSP_MAX_REGS; i++)
		set_bit(PSP_REG_BAR(ndev, i), &bars);

	set_bit(xdna->dev_info->sram_bar, &bars);
	set_bit(xdna->dev_info->smu_bar, &bars);
	set_bit(xdna->dev_info->mbox_bar, &bars);

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (!test_bit(i, &bars))
			continue;
		tbl[i] = pcim_iomap(pdev, i, 0);
		if (!tbl[i]) {
			XDNA_ERR(xdna, "map bar %d failed", i);
			ret = -ENOMEM;
			goto release_fw;
		}
	}

	ndev->sram_base = tbl[xdna->dev_info->sram_bar];
	ndev->smu_base = tbl[xdna->dev_info->smu_bar];
	ndev->mbox_base = tbl[xdna->dev_info->mbox_bar];

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		XDNA_ERR(xdna, "Failed to set DMA mask: %d", ret);
		goto release_fw;
	}

	nvec = pci_msix_vec_count(pdev);
	if (nvec <= 0) {
		XDNA_ERR(xdna, "does not get number of interrupt vector");
		ret = -EINVAL;
		goto release_fw;
	}

	ret = pci_alloc_irq_vectors(pdev, nvec, nvec, PCI_IRQ_MSIX);
	if (ret < 0) {
		XDNA_ERR(xdna, "failed to alloc irq vectors, ret %d", ret);
		goto release_fw;
	}

	ret = iommu_dev_enable_feature(&pdev->dev, IOMMU_DEV_FEAT_SVA);
	if (ret) {
		XDNA_ERR(xdna, "Enable PASID failed, ret %d", ret);
		goto free_irq;
	}

	psp_conf.fw_size = fw->size;
	psp_conf.fw_buf = fw->data;
	for (i = 0; i < PSP_MAX_REGS; i++)
		psp_conf.psp_regs[i] = tbl[PSP_REG_BAR(ndev, i)] + PSP_REG_OFF(ndev, i);
	ndev->psp_hdl = aie2m_psp_create(&xdna->ddev, &psp_conf);
	if (!ndev->psp_hdl) {
		XDNA_ERR(xdna, "failed to create psp");
		ret = -ENOMEM;
		goto disable_sva;
	}
	xdna->dev_handle = ndev;

	ret = aie2_hw_start(xdna);
	if (ret) {
		XDNA_ERR(xdna, "start npu failed, ret %d", ret);
		goto disable_sva;
	}

	ret = aie2_mgmt_fw_query(ndev);
	if (ret) {
		XDNA_ERR(xdna, "Query firmware failed, ret %d", ret);
		goto stop_hw;
	}
	ndev->total_col = ndev->metadata.cols;

	release_firmware(fw);
	return 0;

stop_hw:
	aie2_hw_stop(xdna);
disable_sva:
	iommu_dev_disable_feature(&pdev->dev, IOMMU_DEV_FEAT_SVA);
free_irq:
	pci_free_irq_vectors(pdev);
release_fw:
	release_firmware(fw);

	return ret;
}

static void aie2_fini(struct amdxdna_dev *xdna)
{
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);

	aie2_hw_stop(xdna);
	iommu_dev_disable_feature(&pdev->dev, IOMMU_DEV_FEAT_SVA);
	pci_free_irq_vectors(pdev);
}

const struct amdxdna_dev_ops aie2_ops = {
	.init           = aie2_init,
	.fini           = aie2_fini,
};
