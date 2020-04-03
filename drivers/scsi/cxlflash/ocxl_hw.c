// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CXL Flash Device Driver
 *
 * Written by: Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *             Uma Krishnan <ukrishn@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2018 IBM Corporation
 */

#include <linux/file.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/pseudo_fs.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include <linux/interrupt.h>
#include <asm/xive.h>
#include <misc/ocxl.h>

#include <uapi/misc/cxl.h>

#include "backend.h"
#include "ocxl_hw.h"

/*
 * Pseudo-filesystem to allocate inodes.
 */

#define OCXLFLASH_FS_MAGIC      0x1697698f

static int ocxlflash_fs_cnt;
static struct vfsmount *ocxlflash_vfs_mount;

static int ocxlflash_fs_init_fs_context(struct fs_context *fc)
{
	return init_pseudo(fc, OCXLFLASH_FS_MAGIC) ? 0 : -ENOMEM;
}

static struct file_system_type ocxlflash_fs_type = {
	.name		= "ocxlflash",
	.owner		= THIS_MODULE,
	.init_fs_context = ocxlflash_fs_init_fs_context,
	.kill_sb	= kill_anon_super,
};

/*
 * ocxlflash_release_mapping() - release the memory mapping
 * @ctx:	Context whose mapping is to be released.
 */
static void ocxlflash_release_mapping(struct ocxlflash_context *ctx)
{
	if (ctx->mapping)
		simple_release_fs(&ocxlflash_vfs_mount, &ocxlflash_fs_cnt);
	ctx->mapping = NULL;
}

/*
 * ocxlflash_getfile() - allocate pseudo filesystem, inode, and the file
 * @dev:	Generic device of the host.
 * @name:	Name of the pseudo filesystem.
 * @fops:	File operations.
 * @priv:	Private data.
 * @flags:	Flags for the file.
 *
 * Return: pointer to the file on success, ERR_PTR on failure
 */
static struct file *ocxlflash_getfile(struct device *dev, const char *name,
				      const struct file_operations *fops,
				      void *priv, int flags)
{
	struct file *file;
	struct inode *inode;
	int rc;

	if (fops->owner && !try_module_get(fops->owner)) {
		dev_err(dev, "%s: Owner does not exist\n", __func__);
		rc = -ENOENT;
		goto err1;
	}

	rc = simple_pin_fs(&ocxlflash_fs_type, &ocxlflash_vfs_mount,
			   &ocxlflash_fs_cnt);
	if (unlikely(rc < 0)) {
		dev_err(dev, "%s: Cannot mount ocxlflash pseudofs rc=%d\n",
			__func__, rc);
		goto err2;
	}

	inode = alloc_anon_inode(ocxlflash_vfs_mount->mnt_sb);
	if (IS_ERR(inode)) {
		rc = PTR_ERR(inode);
		dev_err(dev, "%s: alloc_anon_inode failed rc=%d\n",
			__func__, rc);
		goto err3;
	}

	file = alloc_file_pseudo(inode, ocxlflash_vfs_mount, name,
				 flags & (O_ACCMODE | O_NONBLOCK), fops);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		dev_err(dev, "%s: alloc_file failed rc=%d\n",
			__func__, rc);
		goto err4;
	}

	file->private_data = priv;
out:
	return file;
err4:
	iput(inode);
err3:
	simple_release_fs(&ocxlflash_vfs_mount, &ocxlflash_fs_cnt);
err2:
	module_put(fops->owner);
err1:
	file = ERR_PTR(rc);
	goto out;
}

/**
 * ocxlflash_psa_map() - map the process specific MMIO space
 * @ctx_cookie:	Adapter context for which the mapping needs to be done.
 *
 * Return: MMIO pointer of the mapped region
 */
static void __iomem *ocxlflash_psa_map(void *ctx_cookie)
{
	struct ocxlflash_context *ctx = ctx_cookie;
	struct device *dev = ctx->hw_afu->dev;

	mutex_lock(&ctx->state_mutex);
	if (ctx->state != STARTED) {
		dev_err(dev, "%s: Context not started, state=%d\n", __func__,
			ctx->state);
		mutex_unlock(&ctx->state_mutex);
		return NULL;
	}
	mutex_unlock(&ctx->state_mutex);

	return ioremap(ctx->psn_phys, ctx->psn_size);
}

/**
 * ocxlflash_psa_unmap() - unmap the process specific MMIO space
 * @addr:	MMIO pointer to unmap.
 */
static void ocxlflash_psa_unmap(void __iomem *addr)
{
	iounmap(addr);
}

/**
 * ocxlflash_process_element() - get process element of the adapter context
 * @ctx_cookie:	Adapter context associated with the process element.
 *
 * Return: process element of the adapter context
 */
static int ocxlflash_process_element(void *ctx_cookie)
{
	struct ocxlflash_context *ctx = ctx_cookie;

	return ctx->pe;
}

/**
 * afu_map_irq() - map the interrupt of the adapter context
 * @flags:	Flags.
 * @ctx:	Adapter context.
 * @num:	Per-context AFU interrupt number.
 * @handler:	Interrupt handler to register.
 * @cookie:	Interrupt handler private data.
 * @name:	Name of the interrupt.
 *
 * Return: 0 on success, -errno on failure
 */
static int afu_map_irq(u64 flags, struct ocxlflash_context *ctx, int num,
		       irq_handler_t handler, void *cookie, char *name)
{
	struct ocxl_hw_afu *afu = ctx->hw_afu;
	struct device *dev = afu->dev;
	struct ocxlflash_irqs *irq;
	struct xive_irq_data *xd;
	u32 virq;
	int rc = 0;

	if (num < 0 || num >= ctx->num_irqs) {
		dev_err(dev, "%s: Interrupt %d not allocated\n", __func__, num);
		rc = -ENOENT;
		goto out;
	}

	irq = &ctx->irqs[num];
	virq = irq_create_mapping(NULL, irq->hwirq);
	if (unlikely(!virq)) {
		dev_err(dev, "%s: irq_create_mapping failed\n", __func__);
		rc = -ENOMEM;
		goto out;
	}

	rc = request_irq(virq, handler, 0, name, cookie);
	if (unlikely(rc)) {
		dev_err(dev, "%s: request_irq failed rc=%d\n", __func__, rc);
		goto err1;
	}

	xd = irq_get_handler_data(virq);
	if (unlikely(!xd)) {
		dev_err(dev, "%s: Can't get interrupt data\n", __func__);
		rc = -ENXIO;
		goto err2;
	}

	irq->virq = virq;
	irq->vtrig = xd->trig_mmio;
out:
	return rc;
err2:
	free_irq(virq, cookie);
err1:
	irq_dispose_mapping(virq);
	goto out;
}

/**
 * ocxlflash_map_afu_irq() - map the interrupt of the adapter context
 * @ctx_cookie:	Adapter context.
 * @num:	Per-context AFU interrupt number.
 * @handler:	Interrupt handler to register.
 * @cookie:	Interrupt handler private data.
 * @name:	Name of the interrupt.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_map_afu_irq(void *ctx_cookie, int num,
				 irq_handler_t handler, void *cookie,
				 char *name)
{
	return afu_map_irq(0, ctx_cookie, num, handler, cookie, name);
}

/**
 * afu_unmap_irq() - unmap the interrupt
 * @flags:	Flags.
 * @ctx:	Adapter context.
 * @num:	Per-context AFU interrupt number.
 * @cookie:	Interrupt handler private data.
 */
static void afu_unmap_irq(u64 flags, struct ocxlflash_context *ctx, int num,
			  void *cookie)
{
	struct ocxl_hw_afu *afu = ctx->hw_afu;
	struct device *dev = afu->dev;
	struct ocxlflash_irqs *irq;

	if (num < 0 || num >= ctx->num_irqs) {
		dev_err(dev, "%s: Interrupt %d not allocated\n", __func__, num);
		return;
	}

	irq = &ctx->irqs[num];

	if (irq_find_mapping(NULL, irq->hwirq)) {
		free_irq(irq->virq, cookie);
		irq_dispose_mapping(irq->virq);
	}

	memset(irq, 0, sizeof(*irq));
}

/**
 * ocxlflash_unmap_afu_irq() - unmap the interrupt
 * @ctx_cookie:	Adapter context.
 * @num:	Per-context AFU interrupt number.
 * @cookie:	Interrupt handler private data.
 */
static void ocxlflash_unmap_afu_irq(void *ctx_cookie, int num, void *cookie)
{
	return afu_unmap_irq(0, ctx_cookie, num, cookie);
}

/**
 * ocxlflash_get_irq_objhndl() - get the object handle for an interrupt
 * @ctx_cookie:	Context associated with the interrupt.
 * @irq:	Interrupt number.
 *
 * Return: effective address of the mapped region
 */
static u64 ocxlflash_get_irq_objhndl(void *ctx_cookie, int irq)
{
	struct ocxlflash_context *ctx = ctx_cookie;

	if (irq < 0 || irq >= ctx->num_irqs)
		return 0;

	return (__force u64)ctx->irqs[irq].vtrig;
}

/**
 * ocxlflash_xsl_fault() - callback when translation error is triggered
 * @data:	Private data provided at callback registration, the context.
 * @addr:	Address that triggered the error.
 * @dsisr:	Value of dsisr register.
 */
static void ocxlflash_xsl_fault(void *data, u64 addr, u64 dsisr)
{
	struct ocxlflash_context *ctx = data;

	spin_lock(&ctx->slock);
	ctx->fault_addr = addr;
	ctx->fault_dsisr = dsisr;
	ctx->pending_fault = true;
	spin_unlock(&ctx->slock);

	wake_up_all(&ctx->wq);
}

/**
 * start_context() - local routine to start a context
 * @ctx:	Adapter context to be started.
 *
 * Assign the context specific MMIO space, add and enable the PE.
 *
 * Return: 0 on success, -errno on failure
 */
static int start_context(struct ocxlflash_context *ctx)
{
	struct ocxl_hw_afu *afu = ctx->hw_afu;
	struct ocxl_afu_config *acfg = &afu->acfg;
	void *link_token = afu->link_token;
	struct device *dev = afu->dev;
	bool master = ctx->master;
	struct mm_struct *mm;
	int rc = 0;
	u32 pid;

	mutex_lock(&ctx->state_mutex);
	if (ctx->state != OPENED) {
		dev_err(dev, "%s: Context state invalid, state=%d\n",
			__func__, ctx->state);
		rc = -EINVAL;
		goto out;
	}

	if (master) {
		ctx->psn_size = acfg->global_mmio_size;
		ctx->psn_phys = afu->gmmio_phys;
	} else {
		ctx->psn_size = acfg->pp_mmio_stride;
		ctx->psn_phys = afu->ppmmio_phys + (ctx->pe * ctx->psn_size);
	}

	/* pid and mm not set for master contexts */
	if (master) {
		pid = 0;
		mm = NULL;
	} else {
		pid = current->mm->context.id;
		mm = current->mm;
	}

	rc = ocxl_link_add_pe(link_token, ctx->pe, pid, 0, 0, mm,
			      ocxlflash_xsl_fault, ctx);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_link_add_pe failed rc=%d\n",
			__func__, rc);
		goto out;
	}

	ctx->state = STARTED;
out:
	mutex_unlock(&ctx->state_mutex);
	return rc;
}

/**
 * ocxlflash_start_context() - start a kernel context
 * @ctx_cookie:	Adapter context to be started.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_start_context(void *ctx_cookie)
{
	struct ocxlflash_context *ctx = ctx_cookie;

	return start_context(ctx);
}

/**
 * ocxlflash_stop_context() - stop a context
 * @ctx_cookie:	Adapter context to be stopped.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_stop_context(void *ctx_cookie)
{
	struct ocxlflash_context *ctx = ctx_cookie;
	struct ocxl_hw_afu *afu = ctx->hw_afu;
	struct ocxl_afu_config *acfg = &afu->acfg;
	struct pci_dev *pdev = afu->pdev;
	struct device *dev = afu->dev;
	enum ocxlflash_ctx_state state;
	int rc = 0;

	mutex_lock(&ctx->state_mutex);
	state = ctx->state;
	ctx->state = CLOSED;
	mutex_unlock(&ctx->state_mutex);
	if (state != STARTED)
		goto out;

	rc = ocxl_config_terminate_pasid(pdev, acfg->dvsec_afu_control_pos,
					 ctx->pe);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_config_terminate_pasid failed rc=%d\n",
			__func__, rc);
		/* If EBUSY, PE could be referenced in future by the AFU */
		if (rc == -EBUSY)
			goto out;
	}

	rc = ocxl_link_remove_pe(afu->link_token, ctx->pe);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_link_remove_pe failed rc=%d\n",
			__func__, rc);
		goto out;
	}
out:
	return rc;
}

/**
 * ocxlflash_afu_reset() - reset the AFU
 * @ctx_cookie:	Adapter context.
 */
static int ocxlflash_afu_reset(void *ctx_cookie)
{
	struct ocxlflash_context *ctx = ctx_cookie;
	struct device *dev = ctx->hw_afu->dev;

	/* Pending implementation from OCXL transport services */
	dev_err_once(dev, "%s: afu_reset() fop not supported\n", __func__);

	/* Silently return success until it is implemented */
	return 0;
}

/**
 * ocxlflash_set_master() - sets the context as master
 * @ctx_cookie:	Adapter context to set as master.
 */
static void ocxlflash_set_master(void *ctx_cookie)
{
	struct ocxlflash_context *ctx = ctx_cookie;

	ctx->master = true;
}

/**
 * ocxlflash_get_context() - obtains the context associated with the host
 * @pdev:	PCI device associated with the host.
 * @afu_cookie:	Hardware AFU associated with the host.
 *
 * Return: returns the pointer to host adapter context
 */
static void *ocxlflash_get_context(struct pci_dev *pdev, void *afu_cookie)
{
	struct ocxl_hw_afu *afu = afu_cookie;

	return afu->ocxl_ctx;
}

/**
 * ocxlflash_dev_context_init() - allocate and initialize an adapter context
 * @pdev:	PCI device associated with the host.
 * @afu_cookie:	Hardware AFU associated with the host.
 *
 * Return: returns the adapter context on success, ERR_PTR on failure
 */
static void *ocxlflash_dev_context_init(struct pci_dev *pdev, void *afu_cookie)
{
	struct ocxl_hw_afu *afu = afu_cookie;
	struct device *dev = afu->dev;
	struct ocxlflash_context *ctx;
	int rc;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (unlikely(!ctx)) {
		dev_err(dev, "%s: Context allocation failed\n", __func__);
		rc = -ENOMEM;
		goto err1;
	}

	idr_preload(GFP_KERNEL);
	rc = idr_alloc(&afu->idr, ctx, 0, afu->max_pasid, GFP_NOWAIT);
	idr_preload_end();
	if (unlikely(rc < 0)) {
		dev_err(dev, "%s: idr_alloc failed rc=%d\n", __func__, rc);
		goto err2;
	}

	spin_lock_init(&ctx->slock);
	init_waitqueue_head(&ctx->wq);
	mutex_init(&ctx->state_mutex);

	ctx->state = OPENED;
	ctx->pe = rc;
	ctx->master = false;
	ctx->mapping = NULL;
	ctx->hw_afu = afu;
	ctx->irq_bitmap = 0;
	ctx->pending_irq = false;
	ctx->pending_fault = false;
out:
	return ctx;
err2:
	kfree(ctx);
err1:
	ctx = ERR_PTR(rc);
	goto out;
}

/**
 * ocxlflash_release_context() - releases an adapter context
 * @ctx_cookie:	Adapter context to be released.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_release_context(void *ctx_cookie)
{
	struct ocxlflash_context *ctx = ctx_cookie;
	struct device *dev;
	int rc = 0;

	if (!ctx)
		goto out;

	dev = ctx->hw_afu->dev;
	mutex_lock(&ctx->state_mutex);
	if (ctx->state >= STARTED) {
		dev_err(dev, "%s: Context in use, state=%d\n", __func__,
			ctx->state);
		mutex_unlock(&ctx->state_mutex);
		rc = -EBUSY;
		goto out;
	}
	mutex_unlock(&ctx->state_mutex);

	idr_remove(&ctx->hw_afu->idr, ctx->pe);
	ocxlflash_release_mapping(ctx);
	kfree(ctx);
out:
	return rc;
}

/**
 * ocxlflash_perst_reloads_same_image() - sets the image reload policy
 * @afu_cookie:	Hardware AFU associated with the host.
 * @image:	Whether to load the same image on PERST.
 */
static void ocxlflash_perst_reloads_same_image(void *afu_cookie, bool image)
{
	struct ocxl_hw_afu *afu = afu_cookie;

	afu->perst_same_image = image;
}

/**
 * ocxlflash_read_adapter_vpd() - reads the adapter VPD
 * @pdev:	PCI device associated with the host.
 * @buf:	Buffer to get the VPD data.
 * @count:	Size of buffer (maximum bytes that can be read).
 *
 * Return: size of VPD on success, -errno on failure
 */
static ssize_t ocxlflash_read_adapter_vpd(struct pci_dev *pdev, void *buf,
					  size_t count)
{
	return pci_read_vpd(pdev, 0, count, buf);
}

/**
 * free_afu_irqs() - internal service to free interrupts
 * @ctx:	Adapter context.
 */
static void free_afu_irqs(struct ocxlflash_context *ctx)
{
	struct ocxl_hw_afu *afu = ctx->hw_afu;
	struct device *dev = afu->dev;
	int i;

	if (!ctx->irqs) {
		dev_err(dev, "%s: Interrupts not allocated\n", __func__);
		return;
	}

	for (i = ctx->num_irqs; i >= 0; i--)
		ocxl_link_free_irq(afu->link_token, ctx->irqs[i].hwirq);

	kfree(ctx->irqs);
	ctx->irqs = NULL;
}

/**
 * alloc_afu_irqs() - internal service to allocate interrupts
 * @ctx:	Context associated with the request.
 * @num:	Number of interrupts requested.
 *
 * Return: 0 on success, -errno on failure
 */
static int alloc_afu_irqs(struct ocxlflash_context *ctx, int num)
{
	struct ocxl_hw_afu *afu = ctx->hw_afu;
	struct device *dev = afu->dev;
	struct ocxlflash_irqs *irqs;
	int rc = 0;
	int hwirq;
	int i;

	if (ctx->irqs) {
		dev_err(dev, "%s: Interrupts already allocated\n", __func__);
		rc = -EEXIST;
		goto out;
	}

	if (num > OCXL_MAX_IRQS) {
		dev_err(dev, "%s: Too many interrupts num=%d\n", __func__, num);
		rc = -EINVAL;
		goto out;
	}

	irqs = kcalloc(num, sizeof(*irqs), GFP_KERNEL);
	if (unlikely(!irqs)) {
		dev_err(dev, "%s: Context irqs allocation failed\n", __func__);
		rc = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num; i++) {
		rc = ocxl_link_irq_alloc(afu->link_token, &hwirq);
		if (unlikely(rc)) {
			dev_err(dev, "%s: ocxl_link_irq_alloc failed rc=%d\n",
				__func__, rc);
			goto err;
		}

		irqs[i].hwirq = hwirq;
	}

	ctx->irqs = irqs;
	ctx->num_irqs = num;
out:
	return rc;
err:
	for (i = i-1; i >= 0; i--)
		ocxl_link_free_irq(afu->link_token, irqs[i].hwirq);
	kfree(irqs);
	goto out;
}

/**
 * ocxlflash_allocate_afu_irqs() - allocates the requested number of interrupts
 * @ctx_cookie:	Context associated with the request.
 * @num:	Number of interrupts requested.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_allocate_afu_irqs(void *ctx_cookie, int num)
{
	return alloc_afu_irqs(ctx_cookie, num);
}

/**
 * ocxlflash_free_afu_irqs() - frees the interrupts of an adapter context
 * @ctx_cookie:	Adapter context.
 */
static void ocxlflash_free_afu_irqs(void *ctx_cookie)
{
	free_afu_irqs(ctx_cookie);
}

/**
 * ocxlflash_unconfig_afu() - unconfigure the AFU
 * @afu: AFU associated with the host.
 */
static void ocxlflash_unconfig_afu(struct ocxl_hw_afu *afu)
{
	if (afu->gmmio_virt) {
		iounmap(afu->gmmio_virt);
		afu->gmmio_virt = NULL;
	}
}

/**
 * ocxlflash_destroy_afu() - destroy the AFU structure
 * @afu_cookie:	AFU to be freed.
 */
static void ocxlflash_destroy_afu(void *afu_cookie)
{
	struct ocxl_hw_afu *afu = afu_cookie;
	int pos;

	if (!afu)
		return;

	ocxlflash_release_context(afu->ocxl_ctx);
	idr_destroy(&afu->idr);

	/* Disable the AFU */
	pos = afu->acfg.dvsec_afu_control_pos;
	ocxl_config_set_afu_state(afu->pdev, pos, 0);

	ocxlflash_unconfig_afu(afu);
	kfree(afu);
}

/**
 * ocxlflash_config_fn() - configure the host function
 * @pdev:	PCI device associated with the host.
 * @afu:	AFU associated with the host.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_config_fn(struct pci_dev *pdev, struct ocxl_hw_afu *afu)
{
	struct ocxl_fn_config *fcfg = &afu->fcfg;
	struct device *dev = &pdev->dev;
	u16 base, enabled, supported;
	int rc = 0;

	/* Read DVSEC config of the function */
	rc = ocxl_config_read_function(pdev, fcfg);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_config_read_function failed rc=%d\n",
			__func__, rc);
		goto out;
	}

	/* Check if function has AFUs defined, only 1 per function supported */
	if (fcfg->max_afu_index >= 0) {
		afu->is_present = true;
		if (fcfg->max_afu_index != 0)
			dev_warn(dev, "%s: Unexpected AFU index value %d\n",
				 __func__, fcfg->max_afu_index);
	}

	rc = ocxl_config_get_actag_info(pdev, &base, &enabled, &supported);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_config_get_actag_info failed rc=%d\n",
			__func__, rc);
		goto out;
	}

	afu->fn_actag_base = base;
	afu->fn_actag_enabled = enabled;

	ocxl_config_set_actag(pdev, fcfg->dvsec_function_pos, base, enabled);
	dev_dbg(dev, "%s: Function acTag range base=%u enabled=%u\n",
		__func__, base, enabled);

	rc = ocxl_link_setup(pdev, 0, &afu->link_token);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_link_setup failed rc=%d\n",
			__func__, rc);
		goto out;
	}

	rc = ocxl_config_set_TL(pdev, fcfg->dvsec_tl_pos);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_config_set_TL failed rc=%d\n",
			__func__, rc);
		goto err;
	}
out:
	return rc;
err:
	ocxl_link_release(pdev, afu->link_token);
	goto out;
}

/**
 * ocxlflash_unconfig_fn() - unconfigure the host function
 * @pdev:	PCI device associated with the host.
 * @afu:	AFU associated with the host.
 */
static void ocxlflash_unconfig_fn(struct pci_dev *pdev, struct ocxl_hw_afu *afu)
{
	ocxl_link_release(pdev, afu->link_token);
}

/**
 * ocxlflash_map_mmio() - map the AFU MMIO space
 * @afu: AFU associated with the host.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_map_mmio(struct ocxl_hw_afu *afu)
{
	struct ocxl_afu_config *acfg = &afu->acfg;
	struct pci_dev *pdev = afu->pdev;
	struct device *dev = afu->dev;
	phys_addr_t gmmio, ppmmio;
	int rc = 0;

	rc = pci_request_region(pdev, acfg->global_mmio_bar, "ocxlflash");
	if (unlikely(rc)) {
		dev_err(dev, "%s: pci_request_region for global failed rc=%d\n",
			__func__, rc);
		goto out;
	}
	gmmio = pci_resource_start(pdev, acfg->global_mmio_bar);
	gmmio += acfg->global_mmio_offset;

	rc = pci_request_region(pdev, acfg->pp_mmio_bar, "ocxlflash");
	if (unlikely(rc)) {
		dev_err(dev, "%s: pci_request_region for pp bar failed rc=%d\n",
			__func__, rc);
		goto err1;
	}
	ppmmio = pci_resource_start(pdev, acfg->pp_mmio_bar);
	ppmmio += acfg->pp_mmio_offset;

	afu->gmmio_virt = ioremap(gmmio, acfg->global_mmio_size);
	if (unlikely(!afu->gmmio_virt)) {
		dev_err(dev, "%s: MMIO mapping failed\n", __func__);
		rc = -ENOMEM;
		goto err2;
	}

	afu->gmmio_phys = gmmio;
	afu->ppmmio_phys = ppmmio;
out:
	return rc;
err2:
	pci_release_region(pdev, acfg->pp_mmio_bar);
err1:
	pci_release_region(pdev, acfg->global_mmio_bar);
	goto out;
}

/**
 * ocxlflash_config_afu() - configure the host AFU
 * @pdev:	PCI device associated with the host.
 * @afu:	AFU associated with the host.
 *
 * Must be called _after_ host function configuration.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_config_afu(struct pci_dev *pdev, struct ocxl_hw_afu *afu)
{
	struct ocxl_afu_config *acfg = &afu->acfg;
	struct ocxl_fn_config *fcfg = &afu->fcfg;
	struct device *dev = &pdev->dev;
	int count;
	int base;
	int pos;
	int rc = 0;

	/* This HW AFU function does not have any AFUs defined */
	if (!afu->is_present)
		goto out;

	/* Read AFU config at index 0 */
	rc = ocxl_config_read_afu(pdev, fcfg, acfg, 0);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxl_config_read_afu failed rc=%d\n",
			__func__, rc);
		goto out;
	}

	/* Only one AFU per function is supported, so actag_base is same */
	base = afu->fn_actag_base;
	count = min_t(int, acfg->actag_supported, afu->fn_actag_enabled);
	pos = acfg->dvsec_afu_control_pos;

	ocxl_config_set_afu_actag(pdev, pos, base, count);
	dev_dbg(dev, "%s: acTag base=%d enabled=%d\n", __func__, base, count);
	afu->afu_actag_base = base;
	afu->afu_actag_enabled = count;
	afu->max_pasid = 1 << acfg->pasid_supported_log;

	ocxl_config_set_afu_pasid(pdev, pos, 0, acfg->pasid_supported_log);

	rc = ocxlflash_map_mmio(afu);
	if (unlikely(rc)) {
		dev_err(dev, "%s: ocxlflash_map_mmio failed rc=%d\n",
			__func__, rc);
		goto out;
	}

	/* Enable the AFU */
	ocxl_config_set_afu_state(pdev, acfg->dvsec_afu_control_pos, 1);
out:
	return rc;
}

/**
 * ocxlflash_create_afu() - create the AFU for OCXL
 * @pdev:	PCI device associated with the host.
 *
 * Return: AFU on success, NULL on failure
 */
static void *ocxlflash_create_afu(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct ocxlflash_context *ctx;
	struct ocxl_hw_afu *afu;
	int rc;

	afu = kzalloc(sizeof(*afu), GFP_KERNEL);
	if (unlikely(!afu)) {
		dev_err(dev, "%s: HW AFU allocation failed\n", __func__);
		goto out;
	}

	afu->pdev = pdev;
	afu->dev = dev;
	idr_init(&afu->idr);

	rc = ocxlflash_config_fn(pdev, afu);
	if (unlikely(rc)) {
		dev_err(dev, "%s: Function configuration failed rc=%d\n",
			__func__, rc);
		goto err1;
	}

	rc = ocxlflash_config_afu(pdev, afu);
	if (unlikely(rc)) {
		dev_err(dev, "%s: AFU configuration failed rc=%d\n",
			__func__, rc);
		goto err2;
	}

	ctx = ocxlflash_dev_context_init(pdev, afu);
	if (IS_ERR(ctx)) {
		rc = PTR_ERR(ctx);
		dev_err(dev, "%s: ocxlflash_dev_context_init failed rc=%d\n",
			__func__, rc);
		goto err3;
	}

	afu->ocxl_ctx = ctx;
out:
	return afu;
err3:
	ocxlflash_unconfig_afu(afu);
err2:
	ocxlflash_unconfig_fn(pdev, afu);
err1:
	idr_destroy(&afu->idr);
	kfree(afu);
	afu = NULL;
	goto out;
}

/**
 * ctx_event_pending() - check for any event pending on the context
 * @ctx:	Context to be checked.
 *
 * Return: true if there is an event pending, false if none pending
 */
static inline bool ctx_event_pending(struct ocxlflash_context *ctx)
{
	if (ctx->pending_irq || ctx->pending_fault)
		return true;

	return false;
}

/**
 * afu_poll() - poll the AFU for events on the context
 * @file:	File associated with the adapter context.
 * @poll:	Poll structure from the user.
 *
 * Return: poll mask
 */
static unsigned int afu_poll(struct file *file, struct poll_table_struct *poll)
{
	struct ocxlflash_context *ctx = file->private_data;
	struct device *dev = ctx->hw_afu->dev;
	ulong lock_flags;
	int mask = 0;

	poll_wait(file, &ctx->wq, poll);

	spin_lock_irqsave(&ctx->slock, lock_flags);
	if (ctx_event_pending(ctx))
		mask |= POLLIN | POLLRDNORM;
	else if (ctx->state == CLOSED)
		mask |= POLLERR;
	spin_unlock_irqrestore(&ctx->slock, lock_flags);

	dev_dbg(dev, "%s: Poll wait completed for pe %i mask %i\n",
		__func__, ctx->pe, mask);

	return mask;
}

/**
 * afu_read() - perform a read on the context for any event
 * @file:	File associated with the adapter context.
 * @buf:	Buffer to receive the data.
 * @count:	Size of buffer (maximum bytes that can be read).
 * @off:	Offset.
 *
 * Return: size of the data read on success, -errno on failure
 */
static ssize_t afu_read(struct file *file, char __user *buf, size_t count,
			loff_t *off)
{
	struct ocxlflash_context *ctx = file->private_data;
	struct device *dev = ctx->hw_afu->dev;
	struct cxl_event event;
	ulong lock_flags;
	ssize_t esize;
	ssize_t rc;
	int bit;
	DEFINE_WAIT(event_wait);

	if (*off != 0) {
		dev_err(dev, "%s: Non-zero offset not supported, off=%lld\n",
			__func__, *off);
		rc = -EINVAL;
		goto out;
	}

	spin_lock_irqsave(&ctx->slock, lock_flags);

	for (;;) {
		prepare_to_wait(&ctx->wq, &event_wait, TASK_INTERRUPTIBLE);

		if (ctx_event_pending(ctx) || (ctx->state == CLOSED))
			break;

		if (file->f_flags & O_NONBLOCK) {
			dev_err(dev, "%s: File cannot be blocked on I/O\n",
				__func__);
			rc = -EAGAIN;
			goto err;
		}

		if (signal_pending(current)) {
			dev_err(dev, "%s: Signal pending on the process\n",
				__func__);
			rc = -ERESTARTSYS;
			goto err;
		}

		spin_unlock_irqrestore(&ctx->slock, lock_flags);
		schedule();
		spin_lock_irqsave(&ctx->slock, lock_flags);
	}

	finish_wait(&ctx->wq, &event_wait);

	memset(&event, 0, sizeof(event));
	event.header.process_element = ctx->pe;
	event.header.size = sizeof(struct cxl_event_header);
	if (ctx->pending_irq) {
		esize = sizeof(struct cxl_event_afu_interrupt);
		event.header.size += esize;
		event.header.type = CXL_EVENT_AFU_INTERRUPT;

		bit = find_first_bit(&ctx->irq_bitmap, ctx->num_irqs);
		clear_bit(bit, &ctx->irq_bitmap);
		event.irq.irq = bit + 1;
		if (bitmap_empty(&ctx->irq_bitmap, ctx->num_irqs))
			ctx->pending_irq = false;
	} else if (ctx->pending_fault) {
		event.header.size += sizeof(struct cxl_event_data_storage);
		event.header.type = CXL_EVENT_DATA_STORAGE;
		event.fault.addr = ctx->fault_addr;
		event.fault.dsisr = ctx->fault_dsisr;
		ctx->pending_fault = false;
	}

	spin_unlock_irqrestore(&ctx->slock, lock_flags);

	if (copy_to_user(buf, &event, event.header.size)) {
		dev_err(dev, "%s: copy_to_user failed\n", __func__);
		rc = -EFAULT;
		goto out;
	}

	rc = event.header.size;
out:
	return rc;
err:
	finish_wait(&ctx->wq, &event_wait);
	spin_unlock_irqrestore(&ctx->slock, lock_flags);
	goto out;
}

/**
 * afu_release() - release and free the context
 * @inode:	File inode pointer.
 * @file:	File associated with the context.
 *
 * Return: 0 on success, -errno on failure
 */
static int afu_release(struct inode *inode, struct file *file)
{
	struct ocxlflash_context *ctx = file->private_data;
	int i;

	/* Unmap and free the interrupts associated with the context */
	for (i = ctx->num_irqs; i >= 0; i--)
		afu_unmap_irq(0, ctx, i, ctx);
	free_afu_irqs(ctx);

	return ocxlflash_release_context(ctx);
}

/**
 * ocxlflash_mmap_fault() - mmap fault handler
 * @vmf:	VM fault associated with current fault.
 *
 * Return: 0 on success, -errno on failure
 */
static vm_fault_t ocxlflash_mmap_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct ocxlflash_context *ctx = vma->vm_file->private_data;
	struct device *dev = ctx->hw_afu->dev;
	u64 mmio_area, offset;

	offset = vmf->pgoff << PAGE_SHIFT;
	if (offset >= ctx->psn_size)
		return VM_FAULT_SIGBUS;

	mutex_lock(&ctx->state_mutex);
	if (ctx->state != STARTED) {
		dev_err(dev, "%s: Context not started, state=%d\n",
			__func__, ctx->state);
		mutex_unlock(&ctx->state_mutex);
		return VM_FAULT_SIGBUS;
	}
	mutex_unlock(&ctx->state_mutex);

	mmio_area = ctx->psn_phys;
	mmio_area += offset;

	return vmf_insert_pfn(vma, vmf->address, mmio_area >> PAGE_SHIFT);
}

static const struct vm_operations_struct ocxlflash_vmops = {
	.fault = ocxlflash_mmap_fault,
};

/**
 * afu_mmap() - map the fault handler operations
 * @file:	File associated with the context.
 * @vma:	VM area associated with mapping.
 *
 * Return: 0 on success, -errno on failure
 */
static int afu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ocxlflash_context *ctx = file->private_data;

	if ((vma_pages(vma) + vma->vm_pgoff) >
	    (ctx->psn_size >> PAGE_SHIFT))
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_ops = &ocxlflash_vmops;
	return 0;
}

static const struct file_operations ocxl_afu_fops = {
	.owner		= THIS_MODULE,
	.poll		= afu_poll,
	.read		= afu_read,
	.release	= afu_release,
	.mmap		= afu_mmap,
};

#define PATCH_FOPS(NAME)						\
	do { if (!fops->NAME) fops->NAME = ocxl_afu_fops.NAME; } while (0)

/**
 * ocxlflash_get_fd() - get file descriptor for an adapter context
 * @ctx_cookie:	Adapter context.
 * @fops:	File operations to be associated.
 * @fd:		File descriptor to be returned back.
 *
 * Return: pointer to the file on success, ERR_PTR on failure
 */
static struct file *ocxlflash_get_fd(void *ctx_cookie,
				     struct file_operations *fops, int *fd)
{
	struct ocxlflash_context *ctx = ctx_cookie;
	struct device *dev = ctx->hw_afu->dev;
	struct file *file;
	int flags, fdtmp;
	int rc = 0;
	char *name = NULL;

	/* Only allow one fd per context */
	if (ctx->mapping) {
		dev_err(dev, "%s: Context is already mapped to an fd\n",
			__func__);
		rc = -EEXIST;
		goto err1;
	}

	flags = O_RDWR | O_CLOEXEC;

	/* This code is similar to anon_inode_getfd() */
	rc = get_unused_fd_flags(flags);
	if (unlikely(rc < 0)) {
		dev_err(dev, "%s: get_unused_fd_flags failed rc=%d\n",
			__func__, rc);
		goto err1;
	}
	fdtmp = rc;

	/* Patch the file ops that are not defined */
	if (fops) {
		PATCH_FOPS(poll);
		PATCH_FOPS(read);
		PATCH_FOPS(release);
		PATCH_FOPS(mmap);
	} else /* Use default ops */
		fops = (struct file_operations *)&ocxl_afu_fops;

	name = kasprintf(GFP_KERNEL, "ocxlflash:%d", ctx->pe);
	file = ocxlflash_getfile(dev, name, fops, ctx, flags);
	kfree(name);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		dev_err(dev, "%s: ocxlflash_getfile failed rc=%d\n",
			__func__, rc);
		goto err2;
	}

	ctx->mapping = file->f_mapping;
	*fd = fdtmp;
out:
	return file;
err2:
	put_unused_fd(fdtmp);
err1:
	file = ERR_PTR(rc);
	goto out;
}

/**
 * ocxlflash_fops_get_context() - get the context associated with the file
 * @file:	File associated with the adapter context.
 *
 * Return: pointer to the context
 */
static void *ocxlflash_fops_get_context(struct file *file)
{
	return file->private_data;
}

/**
 * ocxlflash_afu_irq() - interrupt handler for user contexts
 * @irq:	Interrupt number.
 * @data:	Private data provided at interrupt registration, the context.
 *
 * Return: Always return IRQ_HANDLED.
 */
static irqreturn_t ocxlflash_afu_irq(int irq, void *data)
{
	struct ocxlflash_context *ctx = data;
	struct device *dev = ctx->hw_afu->dev;
	int i;

	dev_dbg(dev, "%s: Interrupt raised for pe %i virq %i\n",
		__func__, ctx->pe, irq);

	for (i = 0; i < ctx->num_irqs; i++) {
		if (ctx->irqs[i].virq == irq)
			break;
	}
	if (unlikely(i >= ctx->num_irqs)) {
		dev_err(dev, "%s: Received AFU IRQ out of range\n", __func__);
		goto out;
	}

	spin_lock(&ctx->slock);
	set_bit(i - 1, &ctx->irq_bitmap);
	ctx->pending_irq = true;
	spin_unlock(&ctx->slock);

	wake_up_all(&ctx->wq);
out:
	return IRQ_HANDLED;
}

/**
 * ocxlflash_start_work() - start a user context
 * @ctx_cookie:	Context to be started.
 * @num_irqs:	Number of interrupts requested.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_start_work(void *ctx_cookie, u64 num_irqs)
{
	struct ocxlflash_context *ctx = ctx_cookie;
	struct ocxl_hw_afu *afu = ctx->hw_afu;
	struct device *dev = afu->dev;
	char *name;
	int rc = 0;
	int i;

	rc = alloc_afu_irqs(ctx, num_irqs);
	if (unlikely(rc < 0)) {
		dev_err(dev, "%s: alloc_afu_irqs failed rc=%d\n", __func__, rc);
		goto out;
	}

	for (i = 0; i < num_irqs; i++) {
		name = kasprintf(GFP_KERNEL, "ocxlflash-%s-pe%i-%i",
				 dev_name(dev), ctx->pe, i);
		rc = afu_map_irq(0, ctx, i, ocxlflash_afu_irq, ctx, name);
		kfree(name);
		if (unlikely(rc < 0)) {
			dev_err(dev, "%s: afu_map_irq failed rc=%d\n",
				__func__, rc);
			goto err;
		}
	}

	rc = start_context(ctx);
	if (unlikely(rc)) {
		dev_err(dev, "%s: start_context failed rc=%d\n", __func__, rc);
		goto err;
	}
out:
	return rc;
err:
	for (i = i-1; i >= 0; i--)
		afu_unmap_irq(0, ctx, i, ctx);
	free_afu_irqs(ctx);
	goto out;
};

/**
 * ocxlflash_fd_mmap() - mmap handler for adapter file descriptor
 * @file:	File installed with adapter file descriptor.
 * @vma:	VM area associated with mapping.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_fd_mmap(struct file *file, struct vm_area_struct *vma)
{
	return afu_mmap(file, vma);
}

/**
 * ocxlflash_fd_release() - release the context associated with the file
 * @inode:	File inode pointer.
 * @file:	File associated with the adapter context.
 *
 * Return: 0 on success, -errno on failure
 */
static int ocxlflash_fd_release(struct inode *inode, struct file *file)
{
	return afu_release(inode, file);
}

/* Backend ops to ocxlflash services */
const struct cxlflash_backend_ops cxlflash_ocxl_ops = {
	.module			= THIS_MODULE,
	.psa_map		= ocxlflash_psa_map,
	.psa_unmap		= ocxlflash_psa_unmap,
	.process_element	= ocxlflash_process_element,
	.map_afu_irq		= ocxlflash_map_afu_irq,
	.unmap_afu_irq		= ocxlflash_unmap_afu_irq,
	.get_irq_objhndl	= ocxlflash_get_irq_objhndl,
	.start_context		= ocxlflash_start_context,
	.stop_context		= ocxlflash_stop_context,
	.afu_reset		= ocxlflash_afu_reset,
	.set_master		= ocxlflash_set_master,
	.get_context		= ocxlflash_get_context,
	.dev_context_init	= ocxlflash_dev_context_init,
	.release_context	= ocxlflash_release_context,
	.perst_reloads_same_image = ocxlflash_perst_reloads_same_image,
	.read_adapter_vpd	= ocxlflash_read_adapter_vpd,
	.allocate_afu_irqs	= ocxlflash_allocate_afu_irqs,
	.free_afu_irqs		= ocxlflash_free_afu_irqs,
	.create_afu		= ocxlflash_create_afu,
	.destroy_afu		= ocxlflash_destroy_afu,
	.get_fd			= ocxlflash_get_fd,
	.fops_get_context	= ocxlflash_fops_get_context,
	.start_work		= ocxlflash_start_work,
	.fd_mmap		= ocxlflash_fd_mmap,
	.fd_release		= ocxlflash_fd_release,
};
