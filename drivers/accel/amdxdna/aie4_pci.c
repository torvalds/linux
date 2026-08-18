// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>
#include <linux/firmware.h>
#include <linux/sizes.h>

#include "aie.h"
#include "aie4_msg_priv.h"
#include "aie4_pci.h"
#include "amdxdna_mailbox.h"
#include "amdxdna_mailbox_helper.h"
#include "amdxdna_pci_drv.h"

#define NO_IOHUB		0
#define PSP_NOTIFY_INTR		0xD007BE11
#define AIE4_TOTAL_COLUMN	3

/*
 * The management mailbox channel is allocated by firmware.
 * The related register and ring buffer information is on SRAM BAR.
 * This struct is the register layout.
 */
struct mailbox_info {
	__u32 valid;
	__u32 protocol_major;
	__u32 protocol_minor;
	__u32 x2i_tail_offset;
	__u32 x2i_head_offset;
	__u32 x2i_buffer_addr;
	__u32 x2i_buffer_size;
	__u32 i2x_tail_offset;
	__u32 i2x_head_offset;
	__u32 i2x_buffer_addr;
	__u32 i2x_buffer_size;
	__u32 i2x_msi_idx;
	__u32 reserved[4];
};

static int aie4_fw_is_alive(struct amdxdna_dev *xdna)
{
	const struct amdxdna_dev_priv *npriv = xdna->dev_info->dev_priv;
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	u32 __iomem *src;
	u32 fw_is_valid;
	int ret;

	src = ndev->rbuf_base + npriv->mbox_info_off;

	ret = readx_poll_timeout(readl, src + offsetof(struct mailbox_info, valid),
				 fw_is_valid, (fw_is_valid == 0x1),
				 AIE_INTERVAL, AIE_TIMEOUT);
	if (ret)
		XDNA_ERR(xdna, "fw_is_valid=%d after %d ms",
			 fw_is_valid, DIV_ROUND_CLOSEST(AIE_TIMEOUT, 1000000));

	return ret;
}

static void aie4_read_mbox_info(struct amdxdna_dev *xdna,
				struct mailbox_info *mbox_info)
{
	const struct amdxdna_dev_priv *npriv = xdna->dev_info->dev_priv;
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	u32 *dst = (u32 *)mbox_info;
	u32 __iomem *src;
	int i;

	src = ndev->rbuf_base + npriv->mbox_info_off;

	for (i = 0; i < sizeof(*mbox_info) / sizeof(u32); i++)
		dst[i] = readl(&src[i]);
}

static int aie4_mailbox_info(struct amdxdna_dev *xdna,
			     struct mailbox_info *mbox_info)
{
	int ret;

	ret = aie4_fw_is_alive(xdna);
	if (ret)
		return ret;

	aie4_read_mbox_info(xdna, mbox_info);

	ret = aie_check_protocol(&xdna->dev_handle->aie,
				 mbox_info->protocol_major,
				 mbox_info->protocol_minor);
	if (ret)
		XDNA_ERR(xdna, "mailbox major.minor %d.%d is not supported",
			 mbox_info->protocol_major, mbox_info->protocol_minor);

	return ret;
}

static void aie4_mailbox_fini(struct amdxdna_dev_hdl *ndev)
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;

	aie_destroy_chann(&ndev->aie, &ndev->aie.mgmt_chann);
	drmm_kfree(&xdna->ddev, ndev->mbox);
	ndev->mbox = NULL;
}

static int aie4_irq_init(struct amdxdna_dev *xdna)
{
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	int ret, nvec;

	nvec = pci_msix_vec_count(pdev);
	XDNA_DBG(xdna, "irq vectors:%d", nvec);
	if (nvec <= 0) {
		XDNA_ERR(xdna, "does not get number of interrupt vector");
		return -EINVAL;
	}

	ret = pci_alloc_irq_vectors(pdev, nvec, nvec, PCI_IRQ_MSIX);
	if (ret < 0) {
		XDNA_ERR(xdna, "failed to alloc irq vector, ret: %d", ret);
		return ret;
	}

	return 0;
}

static int aie4_mailbox_start(struct amdxdna_dev *xdna,
			      struct mailbox_info *mbi)
{
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	const struct amdxdna_dev_priv *npriv = xdna->dev_info->dev_priv;
	struct xdna_mailbox_chann_res *i2x;
	struct xdna_mailbox_chann_res *x2i;
	int mgmt_mb_irq;
	int ret;

	struct xdna_mailbox_res mbox_res = {
		.ringbuf_base = ndev->rbuf_base,
		.ringbuf_size = pci_resource_len(pdev, npriv->mbox_rbuf_bar),
		.mbox_base = ndev->mbox_base,
		.mbox_size = pci_resource_len(pdev, npriv->mbox_bar),
		.name = "xdna_aie4_mailbox",
	};

	i2x = &ndev->aie.mgmt_i2x;
	x2i = &ndev->aie.mgmt_x2i;

	x2i->mb_head_ptr_reg = mbi->x2i_head_offset;
	x2i->mb_tail_ptr_reg = mbi->x2i_tail_offset;
	x2i->rb_start_addr = mbi->x2i_buffer_addr;
	x2i->rb_size = mbi->x2i_buffer_size;

	i2x->rb_start_addr = mbi->i2x_buffer_addr;
	i2x->rb_size = mbi->i2x_buffer_size;
	i2x->mb_head_ptr_reg = mbi->i2x_head_offset;
	i2x->mb_tail_ptr_reg = mbi->i2x_tail_offset;

	ndev->aie.mgmt_chan_idx = mbi->i2x_msi_idx;
	aie_dump_mgmt_chann_debug(&ndev->aie);

	ndev->mbox = xdnam_mailbox_create(&xdna->ddev, &mbox_res);
	if (!ndev->mbox) {
		XDNA_ERR(xdna, "failed to create mailbox device");
		return -ENODEV;
	}

	ndev->aie.mgmt_chann = xdna_mailbox_alloc_channel(ndev->mbox);
	if (!ndev->aie.mgmt_chann) {
		XDNA_ERR(xdna, "failed to alloc mailbox channel");
		return -ENODEV;
	}

	mgmt_mb_irq = pci_irq_vector(pdev, ndev->aie.mgmt_chan_idx);
	if (mgmt_mb_irq < 0) {
		XDNA_ERR(xdna, "failed to alloc irq vector, return %d", mgmt_mb_irq);
		ret = mgmt_mb_irq;
		goto free_channel;
	}

	ret = xdna_mailbox_start_channel(ndev->aie.mgmt_chann,
					 &ndev->aie.mgmt_x2i,
					 &ndev->aie.mgmt_i2x,
					 NO_IOHUB,
					 mgmt_mb_irq);
	if (ret) {
		XDNA_ERR(xdna, "failed to start management mailbox channel");
		ret = -EINVAL;
		goto free_channel;
	}

	XDNA_DBG(xdna, "Mailbox management channel created");
	return 0;

free_channel:
	xdna_mailbox_free_channel(ndev->aie.mgmt_chann);
	ndev->aie.mgmt_chann = NULL;
	return ret;
}

static int aie4_mailbox_init(struct amdxdna_dev_hdl *ndev)
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;
	struct mailbox_info mbox_info;
	int ret;

	ret = aie4_mailbox_info(xdna, &mbox_info);
	if (ret)
		return ret;

	return aie4_mailbox_start(xdna, &mbox_info);
}

static void aie4_fw_stop(struct amdxdna_dev_hdl *ndev)
{
	aie_psp_stop(ndev->aie.psp_hdl);
	aie_smu_fini(ndev->aie.smu_hdl);
}

static int aie4_fw_start(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie_smu_init(ndev->aie.smu_hdl);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "failed to init smu, ret %d", ret);
		return ret;
	}

	ret = aie_psp_start(ndev->aie.psp_hdl);
	if (ret) {
		XDNA_ERR(ndev->aie.xdna, "failed to start psp, ret %d", ret);
		aie_smu_fini(ndev->aie.smu_hdl);
	}

	return ret;
}

static int aie4_partition_init(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE_MSG(aie4_msg_create_partition, AIE4_MSG_OP_CREATE_PARTITION);
	struct amdxdna_dev *xdna = ndev->aie.xdna;
	int ret;

	req.partition_col_start = 0;
	req.partition_col_count = AIE4_TOTAL_COLUMN;
	ret = aie_send_mgmt_msg_wait(&ndev->aie, &msg);
	if (ret) {
		XDNA_ERR(xdna, "partition init failed: %d", ret);
		return ret;
	}

	ndev->partition_id = resp.partition_id;
	return 0;
}

static void aie4_partition_fini(struct amdxdna_dev_hdl *ndev)
{
	DECLARE_AIE_MSG(aie4_msg_destroy_partition, AIE4_MSG_OP_DESTROY_PARTITION);
	struct amdxdna_dev *xdna = ndev->aie.xdna;
	int ret;

	req.partition_id = ndev->partition_id;
	ret = aie_send_mgmt_msg_wait(&ndev->aie, &msg);
	if (ret)
		XDNA_ERR(xdna, "partition fini failed: %d", ret);
}

static int aie4_query(struct amdxdna_dev_hdl *ndev)
{
	return aie4_query_aie_metadata(ndev, &ndev->aie.metadata);
}

static int aie4_pf_hw_start(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie4_fw_start(ndev);
	if (ret)
		return ret;

	ret = aie4_mailbox_init(ndev);
	if (ret)
		goto stop_fw;

	ret = aie4_attach_work_buffer(ndev);
	if (ret)
		goto mbox_fini;

	return 0;

mbox_fini:
	aie4_mailbox_fini(ndev);
stop_fw:
	aie4_fw_stop(ndev);

	return ret;
}

static void aie4_pf_hw_stop(struct amdxdna_dev_hdl *ndev)
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	aie4_suspend_fw(ndev);
	aie4_mailbox_fini(ndev);
	aie4_fw_stop(ndev);
}

static int aie4_vf_hw_start(struct amdxdna_dev_hdl *ndev)
{
	int ret;

	ret = aie4_mailbox_init(ndev);
	if (ret)
		return ret;

	ret = aie4_query(ndev);
	if (ret)
		goto mailbox_fini;

	ret = aie4_partition_init(ndev);
	if (ret)
		goto mailbox_fini;

	return 0;

mailbox_fini:
	aie4_mailbox_fini(ndev);
	return ret;
}

static void aie4_vf_hw_stop(struct amdxdna_dev_hdl *ndev)
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;

	drm_WARN_ON(&xdna->ddev, !mutex_is_locked(&xdna->dev_lock));

	aie4_partition_fini(ndev);
	aie4_mailbox_fini(ndev);
}

static int aie4_request_firmware(struct amdxdna_dev_hdl *ndev,
				 const struct firmware **npufw,
				 const struct firmware **certfw)
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	char fw_name[128];
	int ret;

	ret = snprintf(fw_name, sizeof(fw_name), "amdnpu/%04x_%02x/%s",
		       pdev->device, pdev->revision, ndev->priv->npufw_path);
	if (ret >= sizeof(fw_name)) {
		XDNA_ERR(xdna, "npu firmware path is truncated");
		return -EINVAL;
	}

	ret = request_firmware(npufw, fw_name, &pdev->dev);
	if (ret) {
		XDNA_ERR(xdna, "failed to request_firmware %s, ret %d", fw_name, ret);
		return ret;
	}

	ret = snprintf(fw_name, sizeof(fw_name), "amdnpu/%04x_%02x/%s",
		       pdev->device, pdev->revision, ndev->priv->certfw_path);
	if (ret >= sizeof(fw_name)) {
		XDNA_ERR(xdna, "cert firmware path is truncated");
		ret = -EINVAL;
		goto release_npufw;
	}

	ret = request_firmware(certfw, fw_name, &pdev->dev);
	if (ret) {
		XDNA_ERR(xdna, "failed to request_firmware %s, ret %d", fw_name, ret);
		goto release_npufw;
	}

	return 0;

release_npufw:
	release_firmware(*npufw);

	return ret;
}

static void aie4_release_firmware(struct amdxdna_dev_hdl *ndev,
				  const struct firmware *npufw,
				  const struct firmware *certfw)
{
	release_firmware(certfw);
	release_firmware(npufw);
}

static int aie4_prepare_firmware(struct amdxdna_dev_hdl *ndev,
				 const struct firmware *npufw,
				 const struct firmware *certfw,
				 void __iomem *tbl[PCI_NUM_RESOURCES])
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;
	struct psp_config psp_conf;
	struct smu_config smu_conf;
	int i;

	psp_conf.fw_size = npufw->size;
	psp_conf.fw_buf = npufw->data;
	psp_conf.certfw_size = certfw->size;
	psp_conf.certfw_buf = certfw->data;
	psp_conf.arg2_mask = ~0;
	psp_conf.notify_val = PSP_NOTIFY_INTR;
	for (i = 0; i < PSP_MAX_REGS; i++)
		psp_conf.psp_regs[i] = tbl[PSP_REG_BAR(ndev, i)] + PSP_REG_OFF(ndev, i);
	ndev->aie.psp_hdl = aiem_psp_create(&xdna->ddev, &psp_conf);
	if (!ndev->aie.psp_hdl) {
		XDNA_ERR(xdna, "failed to create psp");
		return -ENOMEM;
	}

	for (i = 0; i < SMU_MAX_REGS; i++)
		smu_conf.smu_regs[i] = tbl[SMU_REG_BAR(ndev, i)] + SMU_REG_OFF(ndev, i);
	ndev->aie.smu_hdl = aiem_smu_create(&xdna->ddev, &smu_conf);
	if (!ndev->aie.smu_hdl) {
		XDNA_ERR(xdna, "failed to create smu");
		return -ENOMEM;
	}

	return 0;
}

static int aie4_load_fw(struct amdxdna_dev_hdl *ndev,
			void __iomem *tbl[PCI_NUM_RESOURCES])
{
	const struct firmware *npufw, *certfw;
	int ret;

	if (!ndev->priv->npufw_path && !ndev->priv->certfw_path)
		return 0;

	ret = aie4_request_firmware(ndev, &npufw, &certfw);
	if (ret)
		return ret;

	ret = aie4_prepare_firmware(ndev, npufw, certfw, tbl);
	aie4_release_firmware(ndev, npufw, certfw);

	return ret;
}

static int aie4m_pcidev_init(struct amdxdna_dev *xdna)
{
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	struct amdxdna_dev_hdl *ndev;
	void __iomem *tbl[PCI_NUM_RESOURCES] = {0};
	unsigned long bars = 0;
	int ret, i;

	ndev = drmm_kzalloc(&xdna->ddev, sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		return -ENOMEM;

	ndev->priv = xdna->dev_info->dev_priv;
	ndev->aie.xdna = xdna;
	xdna->dev_handle = ndev;

	xa_init_flags(&ndev->cert_comp_xa, XA_FLAGS_ALLOC);
	mutex_init(&ndev->cert_comp_lock);

	/* Enable managed PCI device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		XDNA_ERR(xdna, "pcim enable device failed, ret %d", ret);
		return ret;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		XDNA_ERR(xdna, "failed to set DMA mask to 64:%d", ret);
		return ret;
	}

	for (i = 0; i < PSP_MAX_REGS; i++)
		set_bit(PSP_REG_BAR(ndev, i), &bars);
	for (i = 0; i < SMU_MAX_REGS; i++)
		set_bit(SMU_REG_BAR(ndev, i), &bars);
	set_bit(xdna->dev_info->mbox_bar, &bars);
	set_bit(xdna->dev_info->sram_bar, &bars);

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (!test_bit(i, &bars))
			continue;
		tbl[i] = pcim_iomap(pdev, i, 0);
		if (!tbl[i]) {
			XDNA_ERR(xdna, "map bar %d failed", i);
			return -ENOMEM;
		}
	}

	ndev->mbox_base = tbl[xdna->dev_info->mbox_bar];
	ndev->rbuf_base = tbl[xdna->dev_info->sram_bar];

	pci_set_master(pdev);

	ret = aie4_load_fw(ndev, tbl);
	if (ret)
		return ret;

	ret = aie4_irq_init(xdna);
	if (ret)
		return ret;

	amdxdna_vbnv_init(xdna);
	XDNA_DBG(xdna, "init finished");

	return 0;
}

static int aie4_doorbell_mmap(struct amdxdna_client *client, struct vm_area_struct *vma)
{
	struct amdxdna_dev *xdna = client->xdna;
	struct pci_dev *pdev = to_pci_dev(xdna->ddev.dev);
	const struct amdxdna_dev_priv *npriv = xdna->dev_info->dev_priv;
	phys_addr_t res_start;
	unsigned long pfn;
	int ret;

	if (!aie4_hwctx_valid_doorbell(client, vma->vm_pgoff)) {
		XDNA_ERR(xdna, "Invalid doorbell page offset 0x%lx", vma->vm_pgoff);
		return -EINVAL;
	}

	if (vma_pages(vma) != 1) {
		XDNA_ERR(xdna, "can only map one page, got %ld", vma_pages(vma));
		return -EINVAL;
	}

	res_start = pci_resource_start(pdev, xdna->dev_info->doorbell_bar) + npriv->doorbell_off;
	pfn = PHYS_PFN(res_start) + vma->vm_pgoff;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
	ret = io_remap_pfn_range(vma, vma->vm_start,
				 pfn,
				 PAGE_SIZE,
				 vma->vm_page_prot);

	XDNA_DBG(xdna, "doorbell ret %d", ret);
	return ret;
}

static int aie4_get_info(struct amdxdna_client *client, struct amdxdna_drm_get_info *args)
{
	struct amdxdna_dev *xdna = client->xdna;
	struct amdxdna_dev_hdl *ndev = xdna->dev_handle;
	int ret;

	switch (args->param) {
	case DRM_AMDXDNA_QUERY_AIE_METADATA:
		ret = amdxdna_get_metadata(&ndev->aie, client, args);
		break;
	default:
		XDNA_ERR(xdna, "Not supported request parameter %u", args->param);
		ret = -EOPNOTSUPP;
	}

	XDNA_DBG(xdna, "Got param %d", args->param);

	return ret;
}

static int aie4_alloc_work_buffer(struct amdxdna_dev_hdl *ndev)
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;
	u32 buf_size = AIE4_WORK_BUFFER_MIN_SIZE;

	ndev->work_buf = amdxdna_alloc_msg_buffer(xdna, &buf_size,
						  &ndev->work_buf_addr);
	if (IS_ERR(ndev->work_buf)) {
		int ret = PTR_ERR(ndev->work_buf);

		XDNA_ERR(xdna, "Failed to alloc work buffer, size 0x%x",
			 AIE4_WORK_BUFFER_MIN_SIZE);
		ndev->work_buf = NULL;
		return ret;
	}

	ndev->work_buf_size = buf_size;
	XDNA_DBG(xdna, "Work buffer allocated: size 0x%x", buf_size);

	return 0;
}

static void aie4_free_work_buffer(struct amdxdna_dev_hdl *ndev)
{
	struct amdxdna_dev *xdna = ndev->aie.xdna;

	if (!ndev->work_buf)
		return;

	amdxdna_free_msg_buffer(xdna, ndev->work_buf_size, ndev->work_buf,
				ndev->work_buf_addr);
	ndev->work_buf = NULL;
}

static int aie4_pf_init(struct amdxdna_dev *xdna)
{
	int ret;

	ret = aie4m_pcidev_init(xdna);
	if (ret)
		return ret;

	ret = aie4_alloc_work_buffer(xdna->dev_handle);
	if (ret)
		return ret;

	ret = aie4_pf_hw_start(xdna->dev_handle);
	if (ret)
		goto free_work_buf;

	return 0;

free_work_buf:
	aie4_free_work_buffer(xdna->dev_handle);
	return ret;
}

static int aie4_vf_init(struct amdxdna_dev *xdna)
{
	int ret;

	ret = aie4m_pcidev_init(xdna);
	if (ret)
		return ret;

	return aie4_vf_hw_start(xdna->dev_handle);
}

static void aie4_pf_fini(struct amdxdna_dev *xdna)
{
	aie4_sriov_stop(xdna->dev_handle);
	aie4_pf_hw_stop(xdna->dev_handle);
	aie4_free_work_buffer(xdna->dev_handle);
}

static void aie4_vf_fini(struct amdxdna_dev *xdna)
{
	aie4_vf_hw_stop(xdna->dev_handle);
}

const struct amdxdna_dev_ops aie4_pf_ops = {
	.init			= aie4_pf_init,
	.fini			= aie4_pf_fini,
	.sriov_configure        = aie4_sriov_configure,
};

const struct amdxdna_dev_ops aie4_vf_ops = {
	.init			= aie4_vf_init,
	.fini			= aie4_vf_fini,
	.hwctx_init		= aie4_hwctx_init,
	.hwctx_fini		= aie4_hwctx_fini,
	.mmap			= aie4_doorbell_mmap,
	.cmd_wait		= aie4_cmd_wait,
	.get_aie_info		= aie4_get_info,
};
