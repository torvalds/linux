// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "mem_buf_vmperm: " fmt

#include <linux/highmem.h>
#include <linux/mem-buf-exporter.h>
#include "mem-buf-dev.h"
#include "mem-buf-gh.h"
#include "mem-buf-ids.h"

struct mem_buf_vmperm {
	u32 flags;
	int current_vm_perms;
	u32 mapcount;
	int *vmids;
	int *perms;
	unsigned int nr_acl_entries;
	unsigned int max_acl_entries;
	struct dma_buf *dmabuf;
	struct sg_table *sgt;
	gh_memparcel_handle_t memparcel_hdl;
	struct mutex lock;
	mem_buf_dma_buf_destructor dtor;
	void *dtor_data;
};

/*
 * Ensures the vmperm can hold at least nr_acl_entries.
 * Caller must hold vmperm->lock.
 */
static int mem_buf_vmperm_resize(struct mem_buf_vmperm *vmperm,
					u32 new_size)
{
	int *vmids_copy, *perms_copy, *vmids, *perms;
	u32 old_size;

	old_size = vmperm->max_acl_entries;
	if (old_size >= new_size)
		return 0;

	vmids = vmperm->vmids;
	perms = vmperm->perms;
	vmids_copy = kcalloc(new_size, sizeof(*vmids), GFP_KERNEL);
	if (!vmids_copy)
		return -ENOMEM;
	perms_copy = kcalloc(new_size, sizeof(*perms), GFP_KERNEL);
	if (!perms_copy)
		goto out_perms;

	if (vmperm->vmids) {
		memcpy(vmids_copy, vmids, sizeof(*vmids) * old_size);
		kfree(vmids);
	}
	if (vmperm->perms) {
		memcpy(perms_copy, perms, sizeof(*perms) * old_size);
		kfree(perms);
	}
	vmperm->vmids = vmids_copy;
	vmperm->perms = perms_copy;
	vmperm->max_acl_entries = new_size;
	return 0;

out_perms:
	kfree(vmids_copy);
	return -ENOMEM;
}

/*
 * Caller should hold vmperm->lock.
 */
static void mem_buf_vmperm_update_state(struct mem_buf_vmperm *vmperm, int *vmids,
			 int *perms, u32 nr_acl_entries)
{
	int i;
	size_t size = sizeof(*vmids) * nr_acl_entries;

	WARN_ON(vmperm->max_acl_entries < nr_acl_entries);

	memcpy(vmperm->vmids, vmids, size);
	memcpy(vmperm->perms, perms, size);
	vmperm->nr_acl_entries = nr_acl_entries;

	vmperm->current_vm_perms = 0;
	for (i = 0; i < nr_acl_entries; i++) {
		if (vmids[i] == current_vmid)
			vmperm->current_vm_perms = perms[i];
	}
}

/*
 * Some types of errors may leave the memory in an unknown state.
 * Since we cannot guarantee that accessing this memory is safe,
 * acquire an extra reference count to the underlying dmabuf to
 * prevent it from being freed.
 * If this error occurs during dma_buf_release(), the file refcount
 * will already be zero. In this case handling the error is the caller's
 * responsibility.
 */
static void mem_buf_vmperm_set_err(struct mem_buf_vmperm *vmperm)
{
	get_dma_buf(vmperm->dmabuf);
	vmperm->flags |= MEM_BUF_WRAPPER_FLAG_ERR;
}

static struct mem_buf_vmperm *mem_buf_vmperm_alloc_flags(
	struct sg_table *sgt, u32 flags,
	int *vmids, int *perms, u32 nr_acl_entries)
{
	struct mem_buf_vmperm *vmperm;
	int ret;

	vmperm = kzalloc(sizeof(*vmperm), GFP_KERNEL);
	if (!vmperm)
		return ERR_PTR(-ENOMEM);

	mutex_init(&vmperm->lock);
	mutex_lock(&vmperm->lock);
	ret = mem_buf_vmperm_resize(vmperm, nr_acl_entries);
	if (ret)
		goto err_resize_state;

	mem_buf_vmperm_update_state(vmperm, vmids, perms,
					nr_acl_entries);
	mutex_unlock(&vmperm->lock);
	vmperm->sgt = sgt;
	vmperm->flags = flags;
	vmperm->memparcel_hdl = MEM_BUF_MEMPARCEL_INVALID;

	return vmperm;

err_resize_state:
	mutex_unlock(&vmperm->lock);
	kfree(vmperm);
	return ERR_PTR(-ENOMEM);
}

/* Must be freed via mem_buf_vmperm_release. */
struct mem_buf_vmperm *mem_buf_vmperm_alloc_accept(struct sg_table *sgt,
	gh_memparcel_handle_t memparcel_hdl, int *vmids, int *perms,
	unsigned int nr_acl_entries)
{
	struct mem_buf_vmperm *vmperm;

	vmperm = mem_buf_vmperm_alloc_flags(sgt,
		MEM_BUF_WRAPPER_FLAG_ACCEPT,
		vmids, perms, nr_acl_entries);
	if (IS_ERR(vmperm))
		return vmperm;

	vmperm->memparcel_hdl = memparcel_hdl;
	return vmperm;
}
EXPORT_SYMBOL(mem_buf_vmperm_alloc_accept);

struct mem_buf_vmperm *mem_buf_vmperm_alloc_staticvm(struct sg_table *sgt,
	int *vmids, int *perms, u32 nr_acl_entries)
{
	return mem_buf_vmperm_alloc_flags(sgt,
		MEM_BUF_WRAPPER_FLAG_STATIC_VM,
		vmids, perms, nr_acl_entries);
}
EXPORT_SYMBOL(mem_buf_vmperm_alloc_staticvm);

struct mem_buf_vmperm *mem_buf_vmperm_alloc(struct sg_table *sgt)
{
	int vmids[1];
	int perms[1];

	vmids[0] = current_vmid;
	perms[0] = PERM_READ | PERM_WRITE | PERM_EXEC;
	return mem_buf_vmperm_alloc_flags(sgt, 0,
		vmids, perms, 1);
}
EXPORT_SYMBOL(mem_buf_vmperm_alloc);

static int __mem_buf_vmperm_reclaim(struct mem_buf_vmperm *vmperm)
{
	int ret;
	int new_vmids[] = {current_vmid};
	int new_perms[] = {PERM_READ | PERM_WRITE | PERM_EXEC};

	ret = mem_buf_unassign_mem(vmperm->sgt, vmperm->vmids,
				   vmperm->nr_acl_entries,
				   vmperm->memparcel_hdl);
	if (ret) {
		pr_err_ratelimited("Reclaim failed\n");
		mem_buf_vmperm_set_err(vmperm);
		return ret;
	}

	mem_buf_vmperm_update_state(vmperm, new_vmids, new_perms, 1);
	vmperm->flags &= ~MEM_BUF_WRAPPER_FLAG_LENDSHARE;
	vmperm->memparcel_hdl = MEM_BUF_MEMPARCEL_INVALID;
	return 0;
}

static int mem_buf_vmperm_relinquish(struct mem_buf_vmperm *vmperm)
{
	int ret;
	struct gh_sgl_desc *sgl_desc;

	sgl_desc = mem_buf_sgt_to_gh_sgl_desc(vmperm->sgt);
	if (IS_ERR(sgl_desc))
		return PTR_ERR(sgl_desc);

	ret = mem_buf_unmap_mem_s1(sgl_desc);
	kvfree(sgl_desc);
	if (ret)
		return ret;

	ret = mem_buf_unmap_mem_s2(vmperm->memparcel_hdl);
	return ret;
}

int mem_buf_vmperm_release(struct mem_buf_vmperm *vmperm)
{
	int ret = 0;

	if (vmperm->dtor) {
		ret = vmperm->dtor(vmperm->dtor_data);
		if (ret)
			goto exit;
	}

	mutex_lock(&vmperm->lock);
	if (vmperm->flags & MEM_BUF_WRAPPER_FLAG_LENDSHARE)
		ret = __mem_buf_vmperm_reclaim(vmperm);
	else if (vmperm->flags & MEM_BUF_WRAPPER_FLAG_ACCEPT)
		ret = mem_buf_vmperm_relinquish(vmperm);

	mutex_unlock(&vmperm->lock);
exit:
	kfree(vmperm->perms);
	kfree(vmperm->vmids);
	mutex_destroy(&vmperm->lock);
	kfree(vmperm);
	return ret;
}
EXPORT_SYMBOL(mem_buf_vmperm_release);

int mem_buf_dma_buf_attach(struct dma_buf *dmabuf, struct dma_buf_attachment *attachment)
{
	struct mem_buf_dma_buf_ops *ops;

	ops  = container_of(dmabuf->ops, struct mem_buf_dma_buf_ops, dma_ops);
	return ops->attach(dmabuf, attachment);
}
EXPORT_SYMBOL(mem_buf_dma_buf_attach);

struct mem_buf_vmperm *to_mem_buf_vmperm(struct dma_buf *dmabuf)
{
	struct mem_buf_dma_buf_ops *ops;

	if (dmabuf->ops->attach != mem_buf_dma_buf_attach)
		return ERR_PTR(-EINVAL);

	ops = container_of(dmabuf->ops, struct mem_buf_dma_buf_ops, dma_ops);
	return ops->lookup(dmabuf);
}
EXPORT_SYMBOL(to_mem_buf_vmperm);

int mem_buf_dma_buf_set_destructor(struct dma_buf *buf,
				   mem_buf_dma_buf_destructor dtor,
				   void *dtor_data)
{
	struct mem_buf_vmperm *vmperm = to_mem_buf_vmperm(buf);

	if (IS_ERR(vmperm))
		return PTR_ERR(vmperm);

	vmperm->dtor = dtor;
	vmperm->dtor_data = dtor_data;

	return 0;
}
EXPORT_SYMBOL(mem_buf_dma_buf_set_destructor);

/*
 * With CFI enabled, ops->attach must be set from *this* modules in order
 * for the comparison test in to_mem_buf_vmperm() to work.
 */
struct dma_buf *
mem_buf_dma_buf_export(struct dma_buf_export_info *exp_info,
		       struct mem_buf_dma_buf_ops *ops)
{
	struct mem_buf_vmperm *vmperm;
	struct dma_buf *dmabuf;
	struct dma_buf_ops *dma_ops = &ops->dma_ops;

	if (dma_ops->attach != mem_buf_dma_buf_attach) {
		if (!dma_ops->attach) {
			dma_ops->attach = mem_buf_dma_buf_attach;
		} else {
			pr_err("Attach callback must be null! %ps\n", exp_info->ops);
			return ERR_PTR(-EINVAL);
		}
	}
	exp_info->ops = dma_ops;

	dmabuf = dma_buf_export(exp_info);
	if (IS_ERR(dmabuf))
		return dmabuf;

	vmperm = to_mem_buf_vmperm(dmabuf);
	if (WARN_ON(IS_ERR(vmperm))) {
		dma_buf_put(dmabuf);
		return ERR_PTR(-EINVAL);
	}

	vmperm->dmabuf = dmabuf;
	return dmabuf;
}
EXPORT_SYMBOL(mem_buf_dma_buf_export);

void mem_buf_vmperm_pin(struct mem_buf_vmperm *vmperm)
{
	mutex_lock(&vmperm->lock);
	vmperm->mapcount++;
	mutex_unlock(&vmperm->lock);
}
EXPORT_SYMBOL(mem_buf_vmperm_pin);

void mem_buf_vmperm_unpin(struct mem_buf_vmperm *vmperm)
{
	mutex_lock(&vmperm->lock);
	if (!WARN_ON(vmperm->mapcount == 0))
		vmperm->mapcount--;
	mutex_unlock(&vmperm->lock);
}
EXPORT_SYMBOL(mem_buf_vmperm_unpin);

/*
 * DC IVAC requires write permission, so no CMO on read-only buffers.
 * We allow mapping to iommu regardless of permissions.
 * Caller must have previously called mem_buf_vmperm_pin
 */
bool mem_buf_vmperm_can_cmo(struct mem_buf_vmperm *vmperm)
{
	u32 perms = PERM_READ | PERM_WRITE;
	bool ret = false;

	mutex_lock(&vmperm->lock);
	if (((vmperm->current_vm_perms & perms) == perms) && vmperm->mapcount)
		ret = true;
	mutex_unlock(&vmperm->lock);
	return ret;
}
EXPORT_SYMBOL(mem_buf_vmperm_can_cmo);

bool mem_buf_vmperm_can_mmap(struct mem_buf_vmperm *vmperm, struct vm_area_struct *vma)
{
	bool ret = false;

	mutex_lock(&vmperm->lock);
	if (!vmperm->mapcount)
		goto unlock;
	if (!(vmperm->current_vm_perms & PERM_READ))
		goto unlock;
	if (!(vmperm->current_vm_perms & PERM_WRITE) &&
		vma->vm_flags & VM_WRITE)
		goto unlock;
	if (!(vmperm->current_vm_perms & PERM_EXEC) &&
		vma->vm_flags & VM_EXEC)
		goto unlock;
	ret = true;
unlock:
	mutex_unlock(&vmperm->lock);
	return ret;
}
EXPORT_SYMBOL(mem_buf_vmperm_can_mmap);

bool mem_buf_vmperm_can_vmap(struct mem_buf_vmperm *vmperm)
{
	u32 perms = PERM_READ | PERM_WRITE;
	bool ret = false;

	mutex_lock(&vmperm->lock);
	if (((vmperm->current_vm_perms & perms) == perms) && vmperm->mapcount)
		ret = true;
	mutex_unlock(&vmperm->lock);
	return ret;
}
EXPORT_SYMBOL(mem_buf_vmperm_can_vmap);

static int validate_lend_vmids(struct mem_buf_lend_kernel_arg *arg,
				u32 op)
{
	int i;
	bool found = false;

	for (i = 0; i < arg->nr_acl_entries; i++) {
		if (arg->vmids[i] == current_vmid) {
			found = true;
			break;
		}
	}

	if (found && op == GH_RM_TRANS_TYPE_LEND) {
		pr_err_ratelimited("Lend cannot target the current VM\n");
		return -EINVAL;
	} else if (!found && op == GH_RM_TRANS_TYPE_SHARE) {
		pr_err_ratelimited("Share must target the current VM\n");
		return -EINVAL;
	}
	return 0;
}

/*
 * Allow sharing buffers which are not mapped by either mmap, vmap, or dma.
 * Also allow sharing mapped buffers if the new S2 permissions are at least
 * as permissive as the old S2 permissions. Currently differences in
 * executable permission are ignored, under the assumption the memory will
 * not be used for this purpose.
 */
static bool validate_lend_mapcount(struct mem_buf_vmperm *vmperm,
				   struct mem_buf_lend_kernel_arg *arg)
{
	int i;
	int perms = PERM_READ | PERM_WRITE;

	if (!vmperm->mapcount)
		return true;

	for (i = 0; i < arg->nr_acl_entries; i++) {
		if (arg->vmids[i] == current_vmid &&
		    (arg->perms[i] & perms) == perms)
			return true;
	}

	pr_err_ratelimited("%s: dma-buf is pinned, dumping permissions!\n", __func__);
	for (i = 0; i < arg->nr_acl_entries; i++)
		pr_err_ratelimited("%s: VMID=%d PERM=%d\n", __func__,
				    arg->vmids[i], arg->perms[i]);
	return false;
}

static int mem_buf_lend_internal(struct dma_buf *dmabuf,
			struct mem_buf_lend_kernel_arg *arg,
			u32 op)
{
	struct mem_buf_vmperm *vmperm;
	struct sg_table *sgt;
	int ret;

	if (!arg->nr_acl_entries || !arg->vmids || !arg->perms)
		return -EINVAL;

	if (!mem_buf_dev) {
		pr_err("%s: mem-buf driver not probed!\n", __func__);
		return -ENODEV;
	}

	vmperm = to_mem_buf_vmperm(dmabuf);
	if (IS_ERR(vmperm)) {
		pr_err_ratelimited("dmabuf ops %ps are not a mem_buf_dma_buf_ops\n",
				dmabuf->ops);
		return -EINVAL;
	}
	sgt = vmperm->sgt;

	ret = validate_lend_vmids(arg, op);
	if (ret)
		return ret;

	mutex_lock(&vmperm->lock);
	if (vmperm->flags & MEM_BUF_WRAPPER_FLAG_STATIC_VM) {
		pr_err_ratelimited("dma-buf is staticvm type!\n");
		mutex_unlock(&vmperm->lock);
		return -EINVAL;
	}

	if (vmperm->flags & MEM_BUF_WRAPPER_FLAG_LENDSHARE) {
		pr_err_ratelimited("dma-buf already lent or shared!\n");
		mutex_unlock(&vmperm->lock);
		return -EINVAL;
	}

	if (vmperm->flags & MEM_BUF_WRAPPER_FLAG_ACCEPT) {
		pr_err_ratelimited("dma-buf not owned by current vm!\n");
		mutex_unlock(&vmperm->lock);
		return -EINVAL;
	}

	if (!validate_lend_mapcount(vmperm, arg)) {
		mutex_unlock(&vmperm->lock);
		return -EINVAL;
	}

	/*
	 * Although it would be preferrable to require clients to decide
	 * whether they require cache maintenance prior to caling this function
	 * for backwards compatibility with ion we will always do CMO.
	 */
	dma_map_sgtable(mem_buf_dev, vmperm->sgt, DMA_TO_DEVICE, 0);
	dma_unmap_sgtable(mem_buf_dev, vmperm->sgt, DMA_TO_DEVICE, 0);

	ret = mem_buf_vmperm_resize(vmperm, arg->nr_acl_entries);
	if (ret)
		goto err_resize;

	ret = mem_buf_assign_mem(op, vmperm->sgt, arg);
	if (ret) {
		if (ret == -EADDRNOTAVAIL)
			mem_buf_vmperm_set_err(vmperm);
		goto err_assign;
	}

	mem_buf_vmperm_update_state(vmperm, arg->vmids, arg->perms,
			arg->nr_acl_entries);
	vmperm->flags |= MEM_BUF_WRAPPER_FLAG_LENDSHARE;
	vmperm->memparcel_hdl = arg->memparcel_hdl;

	mutex_unlock(&vmperm->lock);
	return 0;

err_assign:
err_resize:
	mutex_unlock(&vmperm->lock);
	return ret;
}

/*
 * Kernel API for Sharing, Lending, Receiving or Reclaiming
 * a dma-buf from a remote Virtual Machine.
 */
int mem_buf_lend(struct dma_buf *dmabuf,
			struct mem_buf_lend_kernel_arg *arg)
{
	return mem_buf_lend_internal(dmabuf, arg, GH_RM_TRANS_TYPE_LEND);
}
EXPORT_SYMBOL(mem_buf_lend);

int mem_buf_share(struct dma_buf *dmabuf,
			struct mem_buf_lend_kernel_arg *arg)
{
	int i, ret, len, found = false;
	int *orig_vmids, *vmids = NULL;
	int *orig_perms, *perms = NULL;

	len = arg->nr_acl_entries;
	for (i = 0; i < len; i++) {
		if (arg->vmids[i] == current_vmid) {
			found = true;
			break;
		}
	}

	if (found)
		return mem_buf_lend_internal(dmabuf, arg, GH_RM_TRANS_TYPE_SHARE);

	vmids = kmalloc_array(len + 1, sizeof(*vmids), GFP_KERNEL);
	if (!vmids)
		return -ENOMEM;

	perms = kmalloc_array(len + 1, sizeof(*perms), GFP_KERNEL);
	if (!perms) {
		kfree(vmids);
		return -ENOMEM;
	}

	/* Add current vmid with RWX permissions to the list */
	memcpy(vmids, arg->vmids, sizeof(*vmids) * len);
	memcpy(perms, arg->perms, sizeof(*perms) * len);
	vmids[len] = current_vmid;
	perms[len] = PERM_READ | PERM_WRITE | PERM_EXEC;

	/* Temporarily switch out the old arrays */
	orig_vmids = arg->vmids;
	orig_perms = arg->perms;
	arg->vmids = vmids;
	arg->perms = perms;
	arg->nr_acl_entries += 1;

	ret = mem_buf_lend_internal(dmabuf, arg, GH_RM_TRANS_TYPE_SHARE);
	/* Swap back */
	arg->vmids = orig_vmids;
	arg->perms = orig_perms;
	arg->nr_acl_entries -= 1;

	kfree(vmids);
	kfree(perms);
	return ret;
}
EXPORT_SYMBOL(mem_buf_share);

int mem_buf_reclaim(struct dma_buf *dmabuf)
{
	struct mem_buf_vmperm *vmperm;
	int ret;

	vmperm = to_mem_buf_vmperm(dmabuf);
	if (IS_ERR(vmperm)) {
		pr_err_ratelimited("dmabuf ops %ps are not a mem_buf_dma_buf_ops\n",
				dmabuf->ops);
		return -EINVAL;
	}

	mutex_lock(&vmperm->lock);
	if (vmperm->flags & MEM_BUF_WRAPPER_FLAG_STATIC_VM) {
		pr_err_ratelimited("dma-buf is staticvm type!\n");
		mutex_unlock(&vmperm->lock);
		return -EINVAL;
	}

	if (!(vmperm->flags & MEM_BUF_WRAPPER_FLAG_LENDSHARE)) {
		pr_err_ratelimited("dma-buf isn't lent or shared!\n");
		mutex_unlock(&vmperm->lock);
		return -EINVAL;
	}

	if (vmperm->flags & MEM_BUF_WRAPPER_FLAG_ACCEPT) {
		pr_err_ratelimited("dma-buf not owned by current vm!\n");
		mutex_unlock(&vmperm->lock);
		return -EINVAL;
	}

	ret = __mem_buf_vmperm_reclaim(vmperm);
	mutex_unlock(&vmperm->lock);
	return ret;
}
EXPORT_SYMBOL(mem_buf_reclaim);

bool mem_buf_dma_buf_exclusive_owner(struct dma_buf *dmabuf)
{
	struct mem_buf_vmperm *vmperm;
	bool ret = false;

	vmperm = to_mem_buf_vmperm(dmabuf);
	if (WARN_ON(IS_ERR(vmperm)))
		return false;

	mutex_lock(&vmperm->lock);
	ret = !vmperm->flags;
	mutex_unlock(&vmperm->lock);
	return ret;
}
EXPORT_SYMBOL(mem_buf_dma_buf_exclusive_owner);

int mem_buf_dma_buf_copy_vmperm(struct dma_buf *dmabuf, int **vmids,
		int **perms, int *nr_acl_entries)
{
	struct mem_buf_vmperm *vmperm;
	size_t size;
	int *vmids_copy, *perms_copy;
	int ret;

	vmperm = to_mem_buf_vmperm(dmabuf);
	if (IS_ERR(vmperm))
		return PTR_ERR(vmperm);

	mutex_lock(&vmperm->lock);
	size = sizeof(*vmids_copy) * vmperm->nr_acl_entries;
	vmids_copy = kmemdup(vmperm->vmids, size, GFP_KERNEL);
	if (!vmids_copy) {
		ret = -ENOMEM;
		goto err_vmids;
	}

	perms_copy = kmemdup(vmperm->perms, size, GFP_KERNEL);
	if (!perms_copy) {
		ret = -ENOMEM;
		goto err_perms;
	}

	*vmids = vmids_copy;
	*perms = perms_copy;
	*nr_acl_entries = vmperm->nr_acl_entries;

	mutex_unlock(&vmperm->lock);
	return 0;

err_perms:
	kfree(vmids_copy);
err_vmids:
	mutex_unlock(&vmperm->lock);
	return ret;
}
EXPORT_SYMBOL(mem_buf_dma_buf_copy_vmperm);

int mem_buf_dma_buf_get_memparcel_hdl(struct dma_buf *dmabuf,
				      gh_memparcel_handle_t *memparcel_hdl)
{
	struct mem_buf_vmperm *vmperm;

	vmperm = to_mem_buf_vmperm(dmabuf);
	if (IS_ERR(vmperm))
		return PTR_ERR(vmperm);

	if (vmperm->memparcel_hdl == MEM_BUF_MEMPARCEL_INVALID)
		return -EINVAL;

	*memparcel_hdl = vmperm->memparcel_hdl;

	return 0;
}
EXPORT_SYMBOL(mem_buf_dma_buf_get_memparcel_hdl);
