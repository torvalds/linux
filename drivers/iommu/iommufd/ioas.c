// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES
 */
#include <linux/interval_tree.h>
#include <linux/iommu.h>
#include <linux/iommufd.h>
#include <uapi/linux/iommufd.h>

#include "io_pagetable.h"

void iommufd_ioas_destroy(struct iommufd_object *obj)
{
	struct iommufd_ioas *ioas = container_of(obj, struct iommufd_ioas, obj);
	int rc;

	rc = iopt_unmap_all(&ioas->iopt, NULL);
	WARN_ON(rc && rc != -ENOENT);
	iopt_destroy_table(&ioas->iopt);
	mutex_destroy(&ioas->mutex);
}

struct iommufd_ioas *iommufd_ioas_alloc(struct iommufd_ctx *ictx)
{
	struct iommufd_ioas *ioas;

	ioas = iommufd_object_alloc(ictx, ioas, IOMMUFD_OBJ_IOAS);
	if (IS_ERR(ioas))
		return ioas;

	iopt_init_table(&ioas->iopt);
	INIT_LIST_HEAD(&ioas->hwpt_list);
	mutex_init(&ioas->mutex);
	return ioas;
}

int iommufd_ioas_alloc_ioctl(struct iommufd_ucmd *ucmd)
{
	struct iommu_ioas_alloc *cmd = ucmd->cmd;
	struct iommufd_ioas *ioas;
	int rc;

	if (cmd->flags)
		return -EOPNOTSUPP;

	ioas = iommufd_ioas_alloc(ucmd->ictx);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	cmd->out_ioas_id = ioas->obj.id;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_table;
	iommufd_object_finalize(ucmd->ictx, &ioas->obj);
	return 0;

out_table:
	iommufd_object_abort_and_destroy(ucmd->ictx, &ioas->obj);
	return rc;
}

int iommufd_ioas_iova_ranges(struct iommufd_ucmd *ucmd)
{
	struct iommu_iova_range __user *ranges;
	struct iommu_ioas_iova_ranges *cmd = ucmd->cmd;
	struct iommufd_ioas *ioas;
	struct interval_tree_span_iter span;
	u32 max_iovas;
	int rc;

	if (cmd->__reserved)
		return -EOPNOTSUPP;

	ioas = iommufd_get_ioas(ucmd->ictx, cmd->ioas_id);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	down_read(&ioas->iopt.iova_rwsem);
	max_iovas = cmd->num_iovas;
	ranges = u64_to_user_ptr(cmd->allowed_iovas);
	cmd->num_iovas = 0;
	cmd->out_iova_alignment = ioas->iopt.iova_alignment;
	interval_tree_for_each_span(&span, &ioas->iopt.reserved_itree, 0,
				    ULONG_MAX) {
		if (!span.is_hole)
			continue;
		if (cmd->num_iovas < max_iovas) {
			struct iommu_iova_range elm = {
				.start = span.start_hole,
				.last = span.last_hole,
			};

			if (copy_to_user(&ranges[cmd->num_iovas], &elm,
					 sizeof(elm))) {
				rc = -EFAULT;
				goto out_put;
			}
		}
		cmd->num_iovas++;
	}
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
	if (rc)
		goto out_put;
	if (cmd->num_iovas > max_iovas)
		rc = -EMSGSIZE;
out_put:
	up_read(&ioas->iopt.iova_rwsem);
	iommufd_put_object(ucmd->ictx, &ioas->obj);
	return rc;
}

static int iommufd_ioas_load_iovas(struct rb_root_cached *itree,
				   struct iommu_iova_range __user *ranges,
				   u32 num)
{
	u32 i;

	for (i = 0; i != num; i++) {
		struct iommu_iova_range range;
		struct iopt_allowed *allowed;

		if (copy_from_user(&range, ranges + i, sizeof(range)))
			return -EFAULT;

		if (range.start >= range.last)
			return -EINVAL;

		if (interval_tree_iter_first(itree, range.start, range.last))
			return -EINVAL;

		allowed = kzalloc(sizeof(*allowed), GFP_KERNEL_ACCOUNT);
		if (!allowed)
			return -ENOMEM;
		allowed->node.start = range.start;
		allowed->node.last = range.last;

		interval_tree_insert(&allowed->node, itree);
	}
	return 0;
}

int iommufd_ioas_allow_iovas(struct iommufd_ucmd *ucmd)
{
	struct iommu_ioas_allow_iovas *cmd = ucmd->cmd;
	struct rb_root_cached allowed_iova = RB_ROOT_CACHED;
	struct interval_tree_node *node;
	struct iommufd_ioas *ioas;
	struct io_pagetable *iopt;
	int rc = 0;

	if (cmd->__reserved)
		return -EOPNOTSUPP;

	ioas = iommufd_get_ioas(ucmd->ictx, cmd->ioas_id);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);
	iopt = &ioas->iopt;

	rc = iommufd_ioas_load_iovas(&allowed_iova,
				     u64_to_user_ptr(cmd->allowed_iovas),
				     cmd->num_iovas);
	if (rc)
		goto out_free;

	/*
	 * We want the allowed tree update to be atomic, so we have to keep the
	 * original nodes around, and keep track of the new nodes as we allocate
	 * memory for them. The simplest solution is to have a new/old tree and
	 * then swap new for old. On success we free the old tree, on failure we
	 * free the new tree.
	 */
	rc = iopt_set_allow_iova(iopt, &allowed_iova);
out_free:
	while ((node = interval_tree_iter_first(&allowed_iova, 0, ULONG_MAX))) {
		interval_tree_remove(node, &allowed_iova);
		kfree(container_of(node, struct iopt_allowed, node));
	}
	iommufd_put_object(ucmd->ictx, &ioas->obj);
	return rc;
}

static int conv_iommu_prot(u32 map_flags)
{
	/*
	 * We provide no manual cache coherency ioctls to userspace and most
	 * architectures make the CPU ops for cache flushing privileged.
	 * Therefore we require the underlying IOMMU to support CPU coherent
	 * operation. Support for IOMMU_CACHE is enforced by the
	 * IOMMU_CAP_CACHE_COHERENCY test during bind.
	 */
	int iommu_prot = IOMMU_CACHE;

	if (map_flags & IOMMU_IOAS_MAP_WRITEABLE)
		iommu_prot |= IOMMU_WRITE;
	if (map_flags & IOMMU_IOAS_MAP_READABLE)
		iommu_prot |= IOMMU_READ;
	return iommu_prot;
}

int iommufd_ioas_map(struct iommufd_ucmd *ucmd)
{
	struct iommu_ioas_map *cmd = ucmd->cmd;
	unsigned long iova = cmd->iova;
	struct iommufd_ioas *ioas;
	unsigned int flags = 0;
	int rc;

	if ((cmd->flags &
	     ~(IOMMU_IOAS_MAP_FIXED_IOVA | IOMMU_IOAS_MAP_WRITEABLE |
	       IOMMU_IOAS_MAP_READABLE)) ||
	    cmd->__reserved)
		return -EOPNOTSUPP;
	if (cmd->iova >= ULONG_MAX || cmd->length >= ULONG_MAX)
		return -EOVERFLOW;

	if (!(cmd->flags &
	      (IOMMU_IOAS_MAP_WRITEABLE | IOMMU_IOAS_MAP_READABLE)))
		return -EINVAL;

	ioas = iommufd_get_ioas(ucmd->ictx, cmd->ioas_id);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	if (!(cmd->flags & IOMMU_IOAS_MAP_FIXED_IOVA))
		flags = IOPT_ALLOC_IOVA;
	rc = iopt_map_user_pages(ucmd->ictx, &ioas->iopt, &iova,
				 u64_to_user_ptr(cmd->user_va), cmd->length,
				 conv_iommu_prot(cmd->flags), flags);
	if (rc)
		goto out_put;

	cmd->iova = iova;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
out_put:
	iommufd_put_object(ucmd->ictx, &ioas->obj);
	return rc;
}

int iommufd_ioas_copy(struct iommufd_ucmd *ucmd)
{
	struct iommu_ioas_copy *cmd = ucmd->cmd;
	struct iommufd_ioas *src_ioas;
	struct iommufd_ioas *dst_ioas;
	unsigned int flags = 0;
	LIST_HEAD(pages_list);
	unsigned long iova;
	int rc;

	iommufd_test_syz_conv_iova_id(ucmd, cmd->src_ioas_id, &cmd->src_iova,
				      &cmd->flags);

	if ((cmd->flags &
	     ~(IOMMU_IOAS_MAP_FIXED_IOVA | IOMMU_IOAS_MAP_WRITEABLE |
	       IOMMU_IOAS_MAP_READABLE)))
		return -EOPNOTSUPP;
	if (cmd->length >= ULONG_MAX || cmd->src_iova >= ULONG_MAX ||
	    cmd->dst_iova >= ULONG_MAX)
		return -EOVERFLOW;

	if (!(cmd->flags &
	      (IOMMU_IOAS_MAP_WRITEABLE | IOMMU_IOAS_MAP_READABLE)))
		return -EINVAL;

	src_ioas = iommufd_get_ioas(ucmd->ictx, cmd->src_ioas_id);
	if (IS_ERR(src_ioas))
		return PTR_ERR(src_ioas);
	rc = iopt_get_pages(&src_ioas->iopt, cmd->src_iova, cmd->length,
			    &pages_list);
	iommufd_put_object(ucmd->ictx, &src_ioas->obj);
	if (rc)
		return rc;

	dst_ioas = iommufd_get_ioas(ucmd->ictx, cmd->dst_ioas_id);
	if (IS_ERR(dst_ioas)) {
		rc = PTR_ERR(dst_ioas);
		goto out_pages;
	}

	if (!(cmd->flags & IOMMU_IOAS_MAP_FIXED_IOVA))
		flags = IOPT_ALLOC_IOVA;
	iova = cmd->dst_iova;
	rc = iopt_map_pages(&dst_ioas->iopt, &pages_list, cmd->length, &iova,
			    conv_iommu_prot(cmd->flags), flags);
	if (rc)
		goto out_put_dst;

	cmd->dst_iova = iova;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));
out_put_dst:
	iommufd_put_object(ucmd->ictx, &dst_ioas->obj);
out_pages:
	iopt_free_pages_list(&pages_list);
	return rc;
}

int iommufd_ioas_unmap(struct iommufd_ucmd *ucmd)
{
	struct iommu_ioas_unmap *cmd = ucmd->cmd;
	struct iommufd_ioas *ioas;
	unsigned long unmapped = 0;
	int rc;

	ioas = iommufd_get_ioas(ucmd->ictx, cmd->ioas_id);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	if (cmd->iova == 0 && cmd->length == U64_MAX) {
		rc = iopt_unmap_all(&ioas->iopt, &unmapped);
		if (rc)
			goto out_put;
	} else {
		if (cmd->iova >= ULONG_MAX || cmd->length >= ULONG_MAX) {
			rc = -EOVERFLOW;
			goto out_put;
		}
		rc = iopt_unmap_iova(&ioas->iopt, cmd->iova, cmd->length,
				     &unmapped);
		if (rc)
			goto out_put;
	}

	cmd->length = unmapped;
	rc = iommufd_ucmd_respond(ucmd, sizeof(*cmd));

out_put:
	iommufd_put_object(ucmd->ictx, &ioas->obj);
	return rc;
}

int iommufd_option_rlimit_mode(struct iommu_option *cmd,
			       struct iommufd_ctx *ictx)
{
	if (cmd->object_id)
		return -EOPNOTSUPP;

	if (cmd->op == IOMMU_OPTION_OP_GET) {
		cmd->val64 = ictx->account_mode == IOPT_PAGES_ACCOUNT_MM;
		return 0;
	}
	if (cmd->op == IOMMU_OPTION_OP_SET) {
		int rc = 0;

		if (!capable(CAP_SYS_RESOURCE))
			return -EPERM;

		xa_lock(&ictx->objects);
		if (!xa_empty(&ictx->objects)) {
			rc = -EBUSY;
		} else {
			if (cmd->val64 == 0)
				ictx->account_mode = IOPT_PAGES_ACCOUNT_USER;
			else if (cmd->val64 == 1)
				ictx->account_mode = IOPT_PAGES_ACCOUNT_MM;
			else
				rc = -EINVAL;
		}
		xa_unlock(&ictx->objects);

		return rc;
	}
	return -EOPNOTSUPP;
}

static int iommufd_ioas_option_huge_pages(struct iommu_option *cmd,
					  struct iommufd_ioas *ioas)
{
	if (cmd->op == IOMMU_OPTION_OP_GET) {
		cmd->val64 = !ioas->iopt.disable_large_pages;
		return 0;
	}
	if (cmd->op == IOMMU_OPTION_OP_SET) {
		if (cmd->val64 == 0)
			return iopt_disable_large_pages(&ioas->iopt);
		if (cmd->val64 == 1) {
			iopt_enable_large_pages(&ioas->iopt);
			return 0;
		}
		return -EINVAL;
	}
	return -EOPNOTSUPP;
}

int iommufd_ioas_option(struct iommufd_ucmd *ucmd)
{
	struct iommu_option *cmd = ucmd->cmd;
	struct iommufd_ioas *ioas;
	int rc = 0;

	if (cmd->__reserved)
		return -EOPNOTSUPP;

	ioas = iommufd_get_ioas(ucmd->ictx, cmd->object_id);
	if (IS_ERR(ioas))
		return PTR_ERR(ioas);

	switch (cmd->option_id) {
	case IOMMU_OPTION_HUGE_PAGES:
		rc = iommufd_ioas_option_huge_pages(cmd, ioas);
		break;
	default:
		rc = -EOPNOTSUPP;
	}

	iommufd_put_object(ucmd->ictx, &ioas->obj);
	return rc;
}
