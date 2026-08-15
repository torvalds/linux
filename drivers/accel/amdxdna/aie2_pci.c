// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <drm/gpu_scheduler.h>
#include <linux/amd-pmf-io.h>
#include <linux/cleanup.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include <linux/xarray.h>
#include <asm/hypervisor.h>

#include "aie2_msg_priv.h"
#include "aie2_pci.h"
#include "aie2_solver.h"
#include "amdxdna_ctx.h"
#include "amdxdna_gem.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_pci_drv.h"
#include "amdxdna_pm.h"

static int aie2_max_col = XRS_MAX_COL;
module_param(aie2_max_col, uint, 0600);
MODULE_PARM_DESC(aie2_max_col, "Maximum column could be used");

#define DEFAULT_TIME_QUANTUM 30000 /* microseconds */

static char *npu_fw[] = {
	"npu_7.sbin",
	"npu.sbin"
};

/*
 * The management mailbox channel is allocated by firmware.
 * The related register and ring buffer information is on SRAM BAR.
 * This struct is the register layout.
 */
#define MGMT_MBOX_MAGIC 0x55504e5f /* _NPU */
struct mgmt_mbox_chann_info {
	__u32	x2i_tail;
	__u32	x2i_head;
	__u32	x2i_buf;
	__u32	x2i_buf_sz;
	__u32	i2x_tail;
	__u32	i2x_head;
	__u32	i2x_buf;
	__u32	i2x_buf_sz;
	__u32	magic;
	__u32	msi_id;
	__u32	prot_major;
	__u32	prot_minor;
	__u32	rsvd[4];
};

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
				 addr, addr, AIE_INTERVAL, AIE_TIMEOUT);
	if (ret || !addr)
		return -ETIME;

	off = AIE2_SRAM_OFF(ndev, addr);
	reg = (u32 *)&info_regs;
	for (i = 0; i < sizeof(info_regs) / sizeof(u32); i++)
		reg[i] = readl(ndev->sram_base + off + i * sizeof(u32));

	if (info_regs.magic != MGMT_MBOX_MAGIC) {
		XDNA_ERR(ndev->aie.xdna, "Invalid mbox magic 0x%x", info_regs.magic);
		ret = -EINVAL;
		goto done;
	}

	i2x = &ndev->aie.mgmt_i2x;
	x2i = &ndev->aie.mgmt_x2i;

	i2x->mb_head_ptr_reg = AIE2_MBOX_OFF(ndev, info_regs.i2x_head);
	i2x->mb_tail_ptr_reg = AIE2_MBOX_OFF(ndev, info_regs.i2x_tail);
	i2x->rb_start_addr   = AIE2_SRAM_OFF(ndev, info_regs.i2x_buf);
	i2x->rb_size         = info_regs.i2x_buf_sz;

	x2i->mb_head_ptr_reg = AIE2_MBOX_OFF(ndev, info_regs.x2i_head);
	x2i->mb_tail_ptr_reg = AIE2_MBOX_OFF(ndev, info_regs.x2i_tail);
	x2i->rb_start_addr   = AIE2_SRAM_OFF(ndev, info_regs.x2i_buf);
	x2i->rb_size         = info_regs.x2i_buf_sz;

	ndev->aie.mgmt_chan_idx  = info_regs.msi_id;
	ndev->aie.mgmt_prot_major = info_regs.prot_major;
	ndev->aie.mgmt_prot_minor = info_regs.prot_minor;

	ret = aie_check_protocol(&ndev->aie, ndev->aie.mgmt_prot_major,
				 ndev->aie.mgmt_prot_minor);

done:
	aie_dump_mgmt_chann_debug(&ndev->aie);

	/* Must clear address at FW_ALIVE_OFF */
	writel(0, SRAM_GET_ADDR(ndev, FW_ALIVE_OFF));

	return ret;
}

int aie2_runtime_cfg(struct amdxdna_dev_hdl *ndev,
		     enum rt_config_category category, u32 *val)
{
	const struct rt_config *cfg;
	u32 value;
	int ret;

	for (cfg = ndev->priv->rt_config; cfg->type; cfg++) {
		if (cfg->category != category)
			continue;

		if (cfg->feature_mask &&
		    bitmap_subset(&cfg->feature_mask, &ndev->aie.feature_mask,
				  AIE2_FEATURE_MAX))
			continue;

		value = val ? *val : cfg->value;
		ret = aie2_set_runtime_cfg(ndev, cfg->type, value);
		if (ret) {
			XDNA_ERR(ndev->aie.xdna, "Set type %d value %d failed",
				 cfg->type, value);
			return ret;
		}
	}

	return 0;
}

static int aie2_xdna_reset(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie2_suspend_fw(ndev);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "Suspend firmware failed");
		return ret;
	}

	ret = aie2_resume_fw(ndev);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "Resume firmware failed");
		return ret;
	}

	return 0;
}

static int aie2_mgmt_fw_init(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie2_runtime_cfg(ndev, AIE2_RT_CFG_INIT, NULL);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "Runtime config failed");
		return ret;
	}

	ret = aie2_assign_mgmt_pasid(ndev, 0);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "Can not assign PASID");
		return ret;
	}

	ret = aie2_update_prop_time_quota(ndev, DEFAULT_TIME_QUANTUM);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "Failed to update execution time quantum");
		return ret;
	}

	ret = aie2_xdna_reset(ndev);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "Reset firmware failed");
		return ret;
	}

	return 0;
}

static int aie2_mgmt_fw_query(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie2_query_firmware_version(ndev, &ndev->aie.xdna->fw_ver);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "query firmware version failed");
		return ret;
	}

	ret = aie2_query_aie_version(ndev, &ndev->version);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "Query AIE version failed");
		return ret;
	}

	ret = aie2_query_aie_metadata(ndev, &ndev->aie.metadata);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "Query AIE metadata failed");
		return ret;
	}

	ndev->total_col = min(aie2_max_col, ndev->aie.metadata.cols);

	return 0;
}

static void aie2_mgmt_fw_fini(struct amdxdna_dev_hdl *ndev)
{
	if (aie2_suspend_fw(ndev))
		XDNA_ERR(ndev->aie.xdna, "Suspend_fw failed");
	XDNA_DBG(ndev->aie.xdna, "Firmware suspended");
}

static int aie2_xrs_load(void *cb_arg, struct xrs_action_load *action)
{
	struct amdxdna_hwctx *hwctx = cb_arg;
	struct amdxdna_dev *xdna;
	int ret;

	xdna = hwctx->client->xdna;

	hwctx->start_col = action->part.start_col;
	hwctx->num_unused_col = action->part.ncols - hwctx->num_col;
	hwctx->num_col = action->part.ncols;
	ret = aie2_create_context(xdna->dev_handle, hwctx);
	if (ret)
		XDNA_ERR(xdna, "create context failed, ret %d", ret);

	return ret;
}

static int aie2_xrs_unload(void *cb_arg)
{
	struct amdxdna_hwctx *hwctx = cb_arg;
	struct amdxdna_dev *xdna;
	int ret;

	xdna = hwctx->client->xdna;

	ret = aie2_destroy_context(xdna->dev_handle, hwctx);
	if (ret)
		XDNA_ERR(xdna, "destroy context failed, ret %d", ret);

	return ret;
}

static int aie2_xrs_set_dft_dpm_level(struct drm_device *ddev, u32 dpm_level)
{
	struct amdxdna_dev *xdna = to_xdna_dev(ddev);
	struct amdxdna_dev_hdl *ndev;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	ndev = xdna->dev_handle;
	ndev->dft_dpm_level = dpm_level;
	if (ndev->pw_mode != POWER_MODE_DEFAULT || ndev->dpm_level == dpm_level)
		return 0;

	return aie2_pm_set_dpm(ndev, dpm_level);
}

static struct xrs_action_ops aie2_xrs_actions = {
	.load = aie2_xrs_load,
	.unload = aie2_xrs_unload,
	.set_dft_dpm_level = aie2_xrs_set_dft_dpm_level,
};

static void aie2_smu_fini(struct amdxdna_dev_hdl *ndev)
{
	ndev->priv->hw_ops->set_dpm(ndev, 0);
	aie_smu_fini(ndev->aie.smu_hdl);
}

static void aie2_hw_stop(struct amdxdna_dev *xdna)
{
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;

	if (ndev->dev_status <= AIE2_DEV_INIT) {
		XDNA_ERR(xdna, "device is already stopped");
		return;
	}

	aie2_runtime_cfg(ndev, AIE2_RT_CFG_CLK_GATING, NULL);
	aie2_mgmt_fw_fini(ndev);
	aie_destroy_chann(&ndev->aie, &ndev->aie.mgmt_chann);
	drmm_kfree(&xdna->ddev, ndev->mbox);
	ndev->mbox = NULL;
	aie_psp_stop(ndev->aie.psp_hdl);
	aie2_smu_fini(ndev);
	aie2_error_async_events_free(ndev);
	pci_disable_device(pdev);

	ndev->dev_status = AIE2_DEV_INIT;
}

static int aie2_hw_start(struct amdxdna_dev *xdna)
{
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	struct xdna_mailbox_res mbox_res;
	u32 xdna_mailbox_intr_reg;
	int mgmt_mb_irq, ret;

	if (ndev->dev_status >= AIE2_DEV_START) {
		XDNA_INFO(xdna, "device is already started");
		return 0;
	}

	ret = pci_enable_device(pdev);
	if (ret) {
		XDNA_ERR(xdna, "failed to enable device, ret %d", ret);
		return ret;
	}
	pci_set_master(pdev);

	mbox_res.ringbuf_base = ndev->sram_base;
	mbox_res.ringbuf_size = pci_resource_len(pdev, xdna->dev_info->sram_bar);
	mbox_res.mbox_base = ndev->mbox_base;
	mbox_res.mbox_size = MBOX_SIZE(ndev);
	mbox_res.name = "xdna_mailbox";
	ndev->mbox = xdnam_mailbox_create(&xdna->ddev, &mbox_res);
	if (!ndev->mbox) {
		XDNA_ERR(xdna, "failed to create mailbox device");
		ret = -ENODEV;
		goto disable_dev;
	}

	ndev->aie.mgmt_chann = xdna_mailbox_alloc_channel(ndev->mbox);
	if (!ndev->aie.mgmt_chann) {
		XDNA_ERR(xdna, "failed to alloc channel");
		ret = -ENODEV;
		goto disable_dev;
	}

	ret = aie_smu_init(ndev->aie.smu_hdl);
	if (ret) {
		XDNA_ERR(xdna, "failed to init smu, ret %d", ret);
		goto free_channel;
	}

	ret = aie_psp_start(ndev->aie.psp_hdl);
	if (ret) {
		XDNA_ERR(xdna, "failed to start psp, ret %d", ret);
		goto fini_smu;
	}

	ret = aie2_get_mgmt_chann_info(ndev);
	if (ret) {
		XDNA_ERR(xdna, "firmware is not alive");
		goto stop_psp;
	}

	mgmt_mb_irq = pci_irq_vector(pdev, ndev->aie.mgmt_chan_idx);
	if (mgmt_mb_irq < 0) {
		ret = mgmt_mb_irq;
		XDNA_ERR(xdna, "failed to alloc irq vector, ret %d", ret);
		goto stop_psp;
	}

	xdna_mailbox_intr_reg = ndev->aie.mgmt_i2x.mb_head_ptr_reg + 4;
	ret = xdna_mailbox_start_channel(ndev->aie.mgmt_chann,
					 &ndev->aie.mgmt_x2i,
					 &ndev->aie.mgmt_i2x,
					 xdna_mailbox_intr_reg,
					 mgmt_mb_irq);
	if (ret) {
		XDNA_ERR(xdna, "failed to start management mailbox channel");
		ret = -EINVAL;
		goto stop_psp;
	}

	ret = aie2_mgmt_fw_init(ndev);
	if (ret) {
		XDNA_ERR(xdna, "initial mgmt firmware failed, ret %d", ret);
		goto stop_fw;
	}

	ret = aie2_pm_init(ndev);
	if (ret) {
		XDNA_ERR(xdna, "failed to init pm, ret %d", ret);
		goto stop_fw;
	}

	ret = aie2_mgmt_fw_query(ndev);
	if (ret) {
		XDNA_ERR(xdna, "failed to query fw, ret %d", ret);
		goto stop_fw;
	}

	ret = aie2_error_async_events_alloc(ndev);
	if (ret) {
		XDNA_ERR(xdna, "Allocate async events failed, ret %d", ret);
		goto stop_fw;
	}

	ndev->dev_status = AIE2_DEV_START;

	return 0;

stop_fw:
	aie2_suspend_fw(ndev);
	xdna_mailbox_stop_channel(ndev->aie.mgmt_chann);
stop_psp:
	aie_psp_stop(ndev->aie.psp_hdl);
fini_smu:
	aie2_smu_fini(ndev);
free_channel:
	xdna_mailbox_free_channel(ndev->aie.mgmt_chann);
	ndev->aie.mgmt_chann = NULL;
disable_dev:
	pci_disable_device(pdev);

	return ret;
}

static int aie2_hw_suspend(struct amdxdna_dev *xdna)
{
	struct amdxdna_client *client;

	list_for_each_entry(client, &xdna->client_list, node)
		aie2_hwctx_suspend(client);

	aie2_hw_stop(xdna);

	return 0;
}

static int aie2_hw_resume(struct amdxdna_dev *xdna)
{
	struct amdxdna_client *client;
	int ret;

	ret = aie2_hw_start(xdna);
	if (ret) {
		XDNA_ERR(xdna, "Start hardware failed, %d", ret);
		return ret;
	}

	list_for_each_entry(client, &xdna->client_list, node) {
		ret = aie2_hwctx_resume(client);
		if (ret)
			break;
	}

	return ret;
}

static int aie2_init(struct amdxdna_dev *xdna)
{
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	void __iomem *tbl[PCI_NUM_RESOURCES] = {0};
	struct init_config xrs_cfg = { 0 };
	struct amdxdna_dev_hdl *ndev;
	struct psp_config psp_conf = { 0 };
	struct smu_config smu_conf;
	const struct firmware *fw;
	unsigned long bars = 0;
	char *fw_full_path;
	int i, nvec, ret;

	if (!hypervisor_is_type(X86_HYPER_NATIVE)) {
		XDNA_ERR(xdna, "Running under hypervisor not supported");
		return -EINVAL;
	}

	if (!xdna->group) {
		XDNA_ERR(xdna, "Running without IOMMU not supported");
		return -EINVAL;
	}

	ndev = drmm_kzalloc(&xdna->ddev, sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		return -ENOMEM;

	ndev->priv = xdna->dev_info->dev_priv;
	ndev->aie.xdna = xdna;

	for (i = 0; i < ARRAY_SIZE(npu_fw); i++) {
		fw_full_path = kasprintf(GFP_KERNEL, "%s%s", ndev->priv->fw_path, npu_fw[i]);
		if (!fw_full_path)
			return -ENOMEM;

		ret = firmware_request_nowarn(&fw, fw_full_path, &pdev->dev);
		kfree(fw_full_path);
		if (!ret) {
			XDNA_INFO(xdna, "Load firmware %s%s", ndev->priv->fw_path, npu_fw[i]);
			break;
		}
	}

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
	for (i = 0; i < SMU_MAX_REGS; i++)
		set_bit(SMU_REG_BAR(ndev, i), &bars);

	set_bit(xdna->dev_info->sram_bar, &bars);
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

	psp_conf.fw_size = fw->size;
	psp_conf.fw_buf = fw->data;
	psp_conf.arg2_mask = GENMASK(23, 0);
	psp_conf.notify_val = 1;
	for (i = 0; i < PSP_MAX_REGS; i++)
		psp_conf.psp_regs[i] = tbl[PSP_REG_BAR(ndev, i)] + PSP_REG_OFF(ndev, i);
	ndev->aie.psp_hdl = aiem_psp_create(&xdna->ddev, &psp_conf);
	if (!ndev->aie.psp_hdl) {
		XDNA_ERR(xdna, "failed to create psp");
		ret = -ENOMEM;
		goto release_fw;
	}

	for (i = 0; i < SMU_MAX_REGS; i++)
		smu_conf.smu_regs[i] = tbl[SMU_REG_BAR(ndev, i)] + SMU_REG_OFF(ndev, i);
	ndev->aie.smu_hdl = aiem_smu_create(&xdna->ddev, &smu_conf);
	if (!ndev->aie.smu_hdl) {
		XDNA_ERR(xdna, "failed to create smu");
		ret = -ENOMEM;
		goto release_fw;
	}
	xdna->dev_handle = ndev;

	ret = aie2_hw_start(xdna);
	if (ret) {
		XDNA_ERR(xdna, "start npu failed, ret %d", ret);
		goto release_fw;
	}

	xrs_cfg.clk_list.num_levels = ndev->max_dpm_level + 1;
	for (i = 0; i < xrs_cfg.clk_list.num_levels; i++)
		xrs_cfg.clk_list.cu_clk_list[i] = ndev->priv->dpm_clk_tbl[i].hclk;
	xrs_cfg.sys_eff_factor = 2;
	xrs_cfg.ddev = &xdna->ddev;
	xrs_cfg.actions = &aie2_xrs_actions;
	xrs_cfg.total_col = ndev->total_col;

	xdna->xrs_hdl = xrsm_init(&xrs_cfg);
	if (!xdna->xrs_hdl) {
		XDNA_ERR(xdna, "Initialize resolver failed");
		ret = -EINVAL;
		goto stop_hw;
	}

	release_firmware(fw);
	aie2_msg_init(ndev);
	amdxdna_vbnv_init(xdna);
	amdxdna_pm_init(xdna);
	return 0;

stop_hw:
	aie2_hw_stop(xdna);
release_fw:
	release_firmware(fw);

	return ret;
}

static void aie2_fini(struct amdxdna_dev *xdna)
{
	amdxdna_pm_fini(xdna);
	aie2_hw_stop(xdna);
}

static int aie2_get_aie_status(struct amdxdna_client *client,
			       struct amdxdna_drm_get_info *args)
{
	struct amdxdna_drm_query_aie_status status = {};
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_dev_hdl *ndev;
	u32 buf_sz;
	int ret;

	ndev = xdna->dev_handle;
	buf_sz = min(args->buffer_size, sizeof(status));
	if (copy_from_user(&status, u64_to_user_ptr(args->buffer), buf_sz)) {
		XDNA_ERR(xdna, "Failed to copy AIE request into kernel");
		return -EFAULT;
	}

	ret = aie2_query_status(ndev, u64_to_user_ptr(status.buffer),
				status.buffer_size, &status.cols_filled);
	if (ret) {
		XDNA_ERR(xdna, "Failed to get AIE status info. Ret: %d", ret);
		return ret;
	}

	if (copy_to_user(u64_to_user_ptr(args->buffer), &status, buf_sz)) {
		XDNA_ERR(xdna, "Failed to copy AIE request info to user space");
		return -EFAULT;
	}

	return 0;
}

static int aie2_get_aie_version(struct amdxdna_client *client,
				struct amdxdna_drm_get_info *args)
{
	struct amdxdna_drm_query_aie_version version;
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_dev_hdl *ndev;
	u32 buf_sz;

	ndev = xdna->dev_handle;
	version.major = ndev->version.major;
	version.minor = ndev->version.minor;

	buf_sz = min(args->buffer_size, sizeof(version));
	if (copy_to_user(u64_to_user_ptr(args->buffer), &version, buf_sz))
		return -EFAULT;

	return 0;
}

static int aie2_get_firmware_version(struct amdxdna_client *client,
				     struct amdxdna_drm_get_info *args)
{
	struct amdxdna_drm_query_firmware_version version;
	struct amdxdna_dev *xdna = client->xdna;
	u32 buf_sz;

	version.major = xdna->fw_ver.major;
	version.minor = xdna->fw_ver.minor;
	version.patch = xdna->fw_ver.sub;
	version.build = xdna->fw_ver.build;

	buf_sz = min(args->buffer_size, sizeof(version));
	if (copy_to_user(u64_to_user_ptr(args->buffer), &version, buf_sz))
		return -EFAULT;

	return 0;
}

static int aie2_get_power_mode(struct amdxdna_client *client,
			       struct amdxdna_drm_get_info *args)
{
	struct amdxdna_drm_get_power_mode mode = {};
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_dev_hdl *ndev;
	u32 buf_sz;

	ndev = xdna->dev_handle;
	mode.power_mode = ndev->pw_mode;

	buf_sz = min(args->buffer_size, sizeof(mode));
	if (copy_to_user(u64_to_user_ptr(args->buffer), &mode, buf_sz))
		return -EFAULT;

	return 0;
}

static int aie2_get_clock_metadata(struct amdxdna_client *client,
				   struct amdxdna_drm_get_info *args)
{
	struct amdxdna_drm_query_clock_metadata *clock;
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_dev_hdl *ndev;
	int ret = 0;
	u32 buf_sz;

	ndev = xdna->dev_handle;
	clock = kzalloc_obj(*clock);
	if (!clock)
		return -ENOMEM;

	aie2_update_counters(ndev);
	snprintf(clock->mp_npu_clock.name, sizeof(clock->mp_npu_clock.name),
		 "MP-NPU Clock");
	clock->mp_npu_clock.freq_mhz = ndev->npuclk_freq;
	snprintf(clock->h_clock.name, sizeof(clock->h_clock.name), "H Clock");
	clock->h_clock.freq_mhz = ndev->hclk_freq;

	buf_sz = min(args->buffer_size, sizeof(*clock));
	if (copy_to_user(u64_to_user_ptr(args->buffer), clock, buf_sz))
		ret = -EFAULT;

	kfree(clock);
	return ret;
}

static int aie2_get_sensors(struct amdxdna_client *client,
			    struct amdxdna_drm_get_info *args)
{
	struct amdxdna_dev_hdl *ndev = client->xdna->dev_handle;
	struct amdxdna_drm_query_sensor sensor = {};
	struct amd_pmf_npu_metrics npu_metrics;
	u32 sensors_count = 0, i;
	int ret;

	ret = AIE2_GET_PMF_NPU_METRICS(&npu_metrics);
	if (ret)
		return ret;

	sensor.type = AMDXDNA_SENSOR_TYPE_POWER;
	sensor.input = npu_metrics.npu_power;
	sensor.unitm = -3;
	scnprintf(sensor.label, sizeof(sensor.label), "Total Power");
	scnprintf(sensor.units, sizeof(sensor.units), "mW");

	if (args->buffer_size < sizeof(sensor))
		goto out;

	if (copy_to_user(u64_to_user_ptr(args->buffer), &sensor, sizeof(sensor)))
		return -EFAULT;

	args->buffer_size -= sizeof(sensor);
	sensors_count++;

	for (i = 0; i < min_t(u32, ndev->total_col, 8); i++) {
		memset(&sensor, 0, sizeof(sensor));
		sensor.input = npu_metrics.npu_busy[i];
		sensor.type = AMDXDNA_SENSOR_TYPE_COLUMN_UTILIZATION;
		sensor.unitm = 0;
		scnprintf(sensor.label, sizeof(sensor.label), "Column %d Utilization", i);
		scnprintf(sensor.units, sizeof(sensor.units), "%%");

		if (args->buffer_size < sizeof(sensor))
			goto out;

		if (copy_to_user(u64_to_user_ptr(args->buffer) + sensors_count * sizeof(sensor),
				 &sensor, sizeof(sensor)))
			return -EFAULT;

		args->buffer_size -= sizeof(sensor);
		sensors_count++;
	}

out:
	args->buffer_size = sensors_count * sizeof(sensor);

	return 0;
}

static int aie2_hwctx_status_cb(struct amdxdna_hwctx *hwctx, void *arg)
{
	struct amdxdna_drm_hwctx_entry *tmp __free(kfree) = NULL;
	struct amdxdna_drm_get_array *array_args = arg;
	struct amdxdna_drm_hwctx_entry __user *buf;
	struct app_health_report report;
	struct amdxdna_dev_hdl *ndev;
	u32 size;
	int ret;

	if (!array_args->num_element)
		return -EINVAL;

	tmp = kzalloc_obj(*tmp);
	if (!tmp)
		return -ENOMEM;

	tmp->pid = hwctx->client->pid;
	tmp->context_id = hwctx->id;
	tmp->start_col = hwctx->start_col;
	tmp->num_col = hwctx->num_col;
	tmp->command_submissions = hwctx->priv->seq;
	tmp->command_completions = hwctx->priv->completed;
	tmp->pasid = hwctx->client->pasid;
	tmp->heap_usage = hwctx->client->heap_usage;
	tmp->priority = hwctx->qos.priority;
	tmp->gops = hwctx->qos.gops;
	tmp->fps = hwctx->qos.fps;
	tmp->dma_bandwidth = hwctx->qos.dma_bandwidth;
	tmp->latency = hwctx->qos.latency;
	tmp->frame_exec_time = hwctx->qos.frame_exec_time;
	tmp->state = AMDXDNA_HWCTX_STATE_ACTIVE;
	ndev = hwctx->client->xdna->dev_handle;
	ret = aie2_query_app_health(ndev, hwctx->fw_ctx_id, &report);
	if (!ret) {
		/* Fill in app health report fields */
		tmp->txn_op_idx = report.txn_op_id;
		tmp->ctx_pc = report.ctx_pc;
		tmp->fatal_error_type = report.fatal_info.fatal_type;
		tmp->fatal_error_exception_type = report.fatal_info.exception_type;
		tmp->fatal_error_exception_pc = report.fatal_info.exception_pc;
		tmp->fatal_error_app_module = report.fatal_info.app_module;
	}

	buf = u64_to_user_ptr(array_args->buffer);
	size = min(sizeof(*tmp), array_args->element_size);

	if (copy_to_user(buf, tmp, size))
		return -EFAULT;

	array_args->buffer += size;
	array_args->num_element--;

	return 0;
}

static int aie2_get_hwctx_status(struct amdxdna_client *client,
				 struct amdxdna_drm_get_info *args)
{
	struct amdxdna_drm_get_array array_args;
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_client *tmp_client;
	int ret;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	array_args.element_size = sizeof(struct amdxdna_drm_query_hwctx);
	array_args.buffer = args->buffer;
	array_args.num_element = args->buffer_size / array_args.element_size;
	list_for_each_entry(tmp_client, &xdna->client_list, node) {
		ret = amdxdna_hwctx_walk(tmp_client, &array_args,
					 aie2_hwctx_status_cb);
		if (ret)
			break;
	}

	args->buffer_size -= (u32)(array_args.buffer - args->buffer);
	return 0;
}

static int aie2_query_resource_info(struct amdxdna_client *client,
				    struct amdxdna_drm_get_info *args)
{
	struct amdxdna_drm_get_resource_info res_info;
	const struct amdxdna_dev_priv *priv;
	struct amdxdna_dev_hdl *ndev;
	struct amdxdna_dev *xdna;
	u32 buf_sz;

	xdna = client->xdna;
	ndev = xdna->dev_handle;
	priv = ndev->priv;

	aie2_update_counters(ndev);
	res_info.npu_clk_max = priv->dpm_clk_tbl[ndev->max_dpm_level].hclk;
	res_info.npu_tops_max = ndev->max_tops;
	res_info.npu_task_max = priv->hwctx_limit;
	res_info.npu_tops_curr = ndev->curr_tops;
	res_info.npu_task_curr = ndev->hwctx_num;

	buf_sz = min(args->buffer_size, sizeof(res_info));
	if (copy_to_user(u64_to_user_ptr(args->buffer), &res_info, buf_sz))
		return -EFAULT;

	return 0;
}

static int aie2_fill_hwctx_map(struct amdxdna_hwctx *hwctx, void *arg)
{
	struct amdxdna_dev *xdna = hwctx->client->xdna;
	u32 *map = arg;

	if (hwctx->fw_ctx_id >= xdna->dev_handle->priv->hwctx_limit) {
		XDNA_ERR(xdna, "Invalid fw ctx id %d/%d ", hwctx->fw_ctx_id,
			 xdna->dev_handle->priv->hwctx_limit);
		return -EINVAL;
	}

	map[hwctx->fw_ctx_id] = hwctx->id;
	return 0;
}

static int aie2_get_telemetry(struct amdxdna_client *client,
			      struct amdxdna_drm_get_info *args)
{
	struct amdxdna_drm_query_telemetry_header *header __free(kfree) = NULL;
	u32 telemetry_data_sz, header_sz, elem_num;
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_client *tmp_client;
	int ret;

	elem_num = xdna->dev_handle->priv->hwctx_limit;
	header_sz = struct_size(header, map, elem_num);
	if (args->buffer_size <= header_sz) {
		XDNA_ERR(xdna, "Invalid buffer size");
		return -EINVAL;
	}
	telemetry_data_sz = args->buffer_size - header_sz;

	header = kzalloc(header_sz, GFP_KERNEL);
	if (!header)
		return -ENOMEM;

	if (copy_from_user(header, u64_to_user_ptr(args->buffer), sizeof(*header))) {
		XDNA_ERR(xdna, "Failed to copy telemetry header from user");
		return -EFAULT;
	}

	header->map_num_elements = elem_num;
	list_for_each_entry(tmp_client, &xdna->client_list, node) {
		ret = amdxdna_hwctx_walk(tmp_client, &header->map,
					 aie2_fill_hwctx_map);
		if (ret)
			return ret;
	}

	ret = aie2_query_telemetry(xdna->dev_handle,
				   u64_to_user_ptr(args->buffer + header_sz),
				   telemetry_data_sz, header);
	if (ret) {
		XDNA_ERR(xdna, "Query telemetry failed ret %d", ret);
		return ret;
	}

	if (copy_to_user(u64_to_user_ptr(args->buffer), header, header_sz)) {
		XDNA_ERR(xdna, "Copy header failed");
		return -EFAULT;
	}

	return 0;
}

static int aie2_get_preempt_state(struct amdxdna_client *client,
				  struct amdxdna_drm_get_info *args)
{
	struct amdxdna_drm_attribute_state state = {};
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_dev_hdl *ndev;
	u32 buf_sz;

	ndev = xdna->dev_handle;
	if (args->param == DRM_AMDXDNA_GET_FORCE_PREEMPT_STATE)
		state.state = ndev->force_preempt_enabled;
	else if (args->param == DRM_AMDXDNA_GET_FRAME_BOUNDARY_PREEMPT_STATE)
		state.state = ndev->frame_boundary_preempt;

	buf_sz = min(args->buffer_size, sizeof(state));
	if (copy_to_user(u64_to_user_ptr(args->buffer), &state, buf_sz))
		return -EFAULT;

	return 0;
}

static int aie2_get_info(struct amdxdna_client *client, struct amdxdna_drm_get_info *args)
{
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	int ret, idx;

	if (!drm_dev_enter(&xdna->ddev, &idx))
		return -ENODEV;

	ret = amdxdna_pm_resume_get_locked(xdna);
	if (ret)
		goto dev_exit;

	switch (args->param) {
	case DRM_AMDXDNA_QUERY_AIE_STATUS:
		ret = aie2_get_aie_status(client, args);
		break;
	case DRM_AMDXDNA_QUERY_AIE_METADATA:
		ret = amdxdna_get_metadata(&ndev->aie, client, args);
		break;
	case DRM_AMDXDNA_QUERY_AIE_VERSION:
		ret = aie2_get_aie_version(client, args);
		break;
	case DRM_AMDXDNA_QUERY_CLOCK_METADATA:
		ret = aie2_get_clock_metadata(client, args);
		break;
	case DRM_AMDXDNA_QUERY_SENSORS:
		ret = aie2_get_sensors(client, args);
		break;
	case DRM_AMDXDNA_QUERY_HW_CONTEXTS:
		ret = aie2_get_hwctx_status(client, args);
		break;
	case DRM_AMDXDNA_QUERY_FIRMWARE_VERSION:
		ret = aie2_get_firmware_version(client, args);
		break;
	case DRM_AMDXDNA_GET_POWER_MODE:
		ret = aie2_get_power_mode(client, args);
		break;
	case DRM_AMDXDNA_QUERY_TELEMETRY:
		ret = aie2_get_telemetry(client, args);
		break;
	case DRM_AMDXDNA_QUERY_RESOURCE_INFO:
		ret = aie2_query_resource_info(client, args);
		break;
	case DRM_AMDXDNA_GET_FORCE_PREEMPT_STATE:
	case DRM_AMDXDNA_GET_FRAME_BOUNDARY_PREEMPT_STATE:
		ret = aie2_get_preempt_state(client, args);
		break;
	default:
		XDNA_ERR(xdna, "Not supported request parameter %u", args->param);
		ret = -EOPNOTSUPP;
	}

	amdxdna_pm_suspend_put(xdna);
	XDNA_DBG(xdna, "Got param %d", args->param);

dev_exit:
	drm_dev_exit(idx);
	return ret;
}

static int aie2_query_ctx_status_array(struct amdxdna_client *client,
				       struct amdxdna_drm_get_array *args)
{
	struct amdxdna_drm_get_array array_args;
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_client *tmp_client;
	int ret;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	if (args->element_size > SZ_4K || args->num_element > SZ_1K) {
		XDNA_DBG(xdna, "Invalid element size %d or number of element %d",
			 args->element_size, args->num_element);
		return -EINVAL;
	}

	array_args.element_size = min(args->element_size,
				      sizeof(struct amdxdna_drm_hwctx_entry));
	array_args.buffer = args->buffer;
	array_args.num_element = args->num_element * args->element_size /
				array_args.element_size;
	list_for_each_entry(tmp_client, &xdna->client_list, node) {
		ret = amdxdna_hwctx_walk(tmp_client, &array_args,
					 aie2_hwctx_status_cb);
		if (ret)
			break;
	}

	args->element_size = array_args.element_size;
	args->num_element = (u32)((array_args.buffer - args->buffer) /
				  args->element_size);

	return 0;
}

static int aie2_get_array(struct amdxdna_client *client,
			  struct amdxdna_drm_get_array *args)
{
	struct amdxdna_dev *xdna = client->xdna;
	int ret, idx;

	if (!drm_dev_enter(&xdna->ddev, &idx))
		return -ENODEV;

	ret = amdxdna_pm_resume_get_locked(xdna);
	if (ret)
		goto dev_exit;

	switch (args->param) {
	case DRM_AMDXDNA_HW_CONTEXT_ALL:
		ret = aie2_query_ctx_status_array(client, args);
		break;
	case DRM_AMDXDNA_HW_LAST_ASYNC_ERR:
		ret = aie2_get_array_async_error(xdna->dev_handle, args);
		break;
	case DRM_AMDXDNA_BO_USAGE:
		ret = amdxdna_drm_get_bo_usage(&xdna->ddev, args);
		break;
	default:
		XDNA_ERR(xdna, "Not supported request parameter %u", args->param);
		ret = -EOPNOTSUPP;
	}

	amdxdna_pm_suspend_put(xdna);
	XDNA_DBG(xdna, "Got param %d", args->param);

dev_exit:
	drm_dev_exit(idx);
	return ret;
}

static int aie2_set_power_mode(struct amdxdna_client *client,
			       struct amdxdna_drm_set_state *args)
{
	struct amdxdna_drm_set_power_mode power_state;
	enum amdxdna_power_mode_type power_mode;
	struct amdxdna_dev *xdna = client->xdna;

	if (copy_from_user(&power_state, u64_to_user_ptr(args->buffer),
			   sizeof(power_state))) {
		XDNA_ERR(xdna, "Failed to copy power mode request into kernel");
		return -EFAULT;
	}

	if (XDNA_MBZ_DBG(xdna, power_state.pad, sizeof(power_state.pad)))
		return -EINVAL;

	power_mode = power_state.power_mode;
	if (power_mode > POWER_MODE_TURBO) {
		XDNA_ERR(xdna, "Invalid power mode %d", power_mode);
		return -EINVAL;
	}

	return aie2_pm_set_mode(xdna->dev_handle, power_mode);
}

static int aie2_set_preempt_state(struct amdxdna_client *client,
				  struct amdxdna_drm_set_state *args)
{
	struct amdxdna_dev_hdl *ndev = client->xdna->dev_handle;
	struct amdxdna_drm_attribute_state state;
	u32 val;
	int ret;

	if (copy_from_user(&state, u64_to_user_ptr(args->buffer), sizeof(state)))
		return -EFAULT;

	if (state.state > 1)
		return -EINVAL;

	if (XDNA_MBZ_DBG(client->xdna, state.pad, sizeof(state.pad)))
		return -EINVAL;

	if (args->param == DRM_AMDXDNA_SET_FORCE_PREEMPT) {
		ndev->force_preempt_enabled = state.state;
	} else if (args->param == DRM_AMDXDNA_SET_FRAME_BOUNDARY_PREEMPT) {
		val = state.state;
		ret = aie2_runtime_cfg(ndev, AIE2_RT_CFG_FRAME_BOUNDARY_PREEMPT,
				       &val);
		if (ret)
			return ret;

		ndev->frame_boundary_preempt = state.state;
	}

	return 0;
}

static int aie2_set_state(struct amdxdna_client *client,
			  struct amdxdna_drm_set_state *args)
{
	struct amdxdna_dev *xdna = client->xdna;
	int ret, idx;

	if (!drm_dev_enter(&xdna->ddev, &idx))
		return -ENODEV;

	ret = amdxdna_pm_resume_get_locked(xdna);
	if (ret)
		goto dev_exit;

	switch (args->param) {
	case DRM_AMDXDNA_SET_POWER_MODE:
		ret = aie2_set_power_mode(client, args);
		break;
	case DRM_AMDXDNA_SET_FORCE_PREEMPT:
	case DRM_AMDXDNA_SET_FRAME_BOUNDARY_PREEMPT:
		ret = aie2_set_preempt_state(client, args);
		break;
	default:
		XDNA_ERR(xdna, "Not supported request parameter %u", args->param);
		ret = -EOPNOTSUPP;
		break;
	}

	amdxdna_pm_suspend_put(xdna);
dev_exit:
	drm_dev_exit(idx);
	return ret;
}

static int aie2_get_dev_rev(struct amdxdna_dev *xdna, u32 *rev)
{
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	enum aie2_dev_revision aie2_rev;
	int ret;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));
	ret = aie2_get_dev_revision(ndev, &aie2_rev);

	if (!ret)
		*rev = (u32)aie2_rev;

	return ret;
}

const struct amdxdna_dev_ops aie2_ops = {
	.init = aie2_init,
	.fini = aie2_fini,
	.resume = aie2_hw_resume,
	.suspend = aie2_hw_suspend,
	.get_aie_info = aie2_get_info,
	.set_aie_state = aie2_set_state,
	.hwctx_init = aie2_hwctx_init,
	.hwctx_fini = aie2_hwctx_fini,
	.hwctx_config = aie2_hwctx_config,
	.hwctx_sync_debug_bo = aie2_hwctx_sync_debug_bo,
	.cmd_submit = aie2_cmd_submit,
	.hmm_invalidate = aie2_hmm_invalidate,
	.get_array = aie2_get_array,
	.get_dev_revision = aie2_get_dev_rev,
	.hwctx_heap_expand = aie2_hwctx_heap_expand,
};
