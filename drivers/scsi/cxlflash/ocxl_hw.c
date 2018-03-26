/*
 * CXL Flash Device Driver
 *
 * Written by: Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *             Uma Krishnan <ukrishn@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2018 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/file.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/mount.h>

#include <misc/ocxl.h>

#include "backend.h"
#include "ocxl_hw.h"

/*
 * Pseudo-filesystem to allocate inodes.
 */

#define OCXLFLASH_FS_MAGIC      0x1697698f

static int ocxlflash_fs_cnt;
static struct vfsmount *ocxlflash_vfs_mount;

static const struct dentry_operations ocxlflash_fs_dops = {
	.d_dname	= simple_dname,
};

/*
 * ocxlflash_fs_mount() - mount the pseudo-filesystem
 * @fs_type:	File system type.
 * @flags:	Flags for the filesystem.
 * @dev_name:	Device name associated with the filesystem.
 * @data:	Data pointer.
 *
 * Return: pointer to the directory entry structure
 */
static struct dentry *ocxlflash_fs_mount(struct file_system_type *fs_type,
					 int flags, const char *dev_name,
					 void *data)
{
	return mount_pseudo(fs_type, "ocxlflash:", NULL, &ocxlflash_fs_dops,
			    OCXLFLASH_FS_MAGIC);
}

static struct file_system_type ocxlflash_fs_type = {
	.name		= "ocxlflash",
	.owner		= THIS_MODULE,
	.mount		= ocxlflash_fs_mount,
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
	struct qstr this;
	struct path path;
	struct file *file;
	struct inode *inode = NULL;
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

	this.name = name;
	this.len = strlen(name);
	this.hash = 0;
	path.dentry = d_alloc_pseudo(ocxlflash_vfs_mount->mnt_sb, &this);
	if (!path.dentry) {
		dev_err(dev, "%s: d_alloc_pseudo failed\n", __func__);
		rc = -ENOMEM;
		goto err4;
	}

	path.mnt = mntget(ocxlflash_vfs_mount);
	d_instantiate(path.dentry, inode);

	file = alloc_file(&path, OPEN_FMODE(flags), fops);
	if (IS_ERR(file)) {
		rc = PTR_ERR(file);
		dev_err(dev, "%s: alloc_file failed rc=%d\n",
			__func__, rc);
		goto err5;
	}

	file->f_flags = flags & (O_ACCMODE | O_NONBLOCK);
	file->private_data = priv;
out:
	return file;
err5:
	path_put(&path);
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
 * start_context() - local routine to start a context
 * @ctx:	Adapter context to be started.
 *
 * Assign the context specific MMIO space.
 *
 * Return: 0 on success, -errno on failure
 */
static int start_context(struct ocxlflash_context *ctx)
{
	struct ocxl_hw_afu *afu = ctx->hw_afu;
	struct ocxl_afu_config *acfg = &afu->acfg;
	bool master = ctx->master;

	if (master) {
		ctx->psn_size = acfg->global_mmio_size;
		ctx->psn_phys = afu->gmmio_phys;
	} else {
		ctx->psn_size = acfg->pp_mmio_stride;
		ctx->psn_phys = afu->ppmmio_phys + (ctx->pe * ctx->psn_size);
	}

	return 0;
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

	ctx->pe = rc;
	ctx->master = false;
	ctx->mapping = NULL;
	ctx->hw_afu = afu;
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
	int rc = 0;

	if (!ctx)
		goto out;

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
out:
	return rc;
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
		goto err1;
	}

	ctx = ocxlflash_dev_context_init(pdev, afu);
	if (IS_ERR(ctx)) {
		rc = PTR_ERR(ctx);
		dev_err(dev, "%s: ocxlflash_dev_context_init failed rc=%d\n",
			__func__, rc);
		goto err2;
	}

	afu->ocxl_ctx = ctx;
out:
	return afu;
err2:
	ocxlflash_unconfig_afu(afu);
err1:
	idr_destroy(&afu->idr);
	kfree(afu);
	afu = NULL;
	goto out;
}

static const struct file_operations ocxl_afu_fops = {
	.owner		= THIS_MODULE,
};

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

	/* Use default ops if there is no fops */
	if (!fops)
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

/* Backend ops to ocxlflash services */
const struct cxlflash_backend_ops cxlflash_ocxl_ops = {
	.module			= THIS_MODULE,
	.psa_map		= ocxlflash_psa_map,
	.psa_unmap		= ocxlflash_psa_unmap,
	.process_element	= ocxlflash_process_element,
	.start_context		= ocxlflash_start_context,
	.set_master		= ocxlflash_set_master,
	.get_context		= ocxlflash_get_context,
	.dev_context_init	= ocxlflash_dev_context_init,
	.release_context	= ocxlflash_release_context,
	.perst_reloads_same_image = ocxlflash_perst_reloads_same_image,
	.read_adapter_vpd	= ocxlflash_read_adapter_vpd,
	.create_afu		= ocxlflash_create_afu,
	.destroy_afu		= ocxlflash_destroy_afu,
	.get_fd			= ocxlflash_get_fd,
	.fops_get_context	= ocxlflash_fops_get_context,
};
