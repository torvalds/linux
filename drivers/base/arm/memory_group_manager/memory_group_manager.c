// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/fs.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/module.h>
#if IS_ENABLED(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#include <linux/version_compat_defs.h>
#endif
#include <linux/mm.h>
#include <linux/memory_group_manager.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0))
#undef DEFINE_SIMPLE_ATTRIBUTE
#define DEFINE_SIMPLE_ATTRIBUTE DEFINE_DEBUGFS_ATTRIBUTE
#define debugfs_create_file debugfs_create_file_unsafe
#endif

#if (KERNEL_VERSION(4, 20, 0) > LINUX_VERSION_CODE)
static inline vm_fault_t vmf_insert_pfn_prot(struct vm_area_struct *vma,
			unsigned long addr, unsigned long pfn, pgprot_t pgprot)
{
	int err;

#if ((KERNEL_VERSION(4, 4, 147) >= LINUX_VERSION_CODE) || \
		((KERNEL_VERSION(4, 6, 0) > LINUX_VERSION_CODE) && \
		 (KERNEL_VERSION(4, 5, 0) <= LINUX_VERSION_CODE)))
	if (pgprot_val(pgprot) != pgprot_val(vma->vm_page_prot))
		return VM_FAULT_SIGBUS;

	err = vm_insert_pfn(vma, addr, pfn);
#else
	err = vm_insert_pfn_prot(vma, addr, pfn, pgprot);
#endif

	if (unlikely(err == -ENOMEM))
		return VM_FAULT_OOM;
	if (unlikely(err < 0 && err != -EBUSY))
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}
#endif

#define IMPORTED_MEMORY_ID (MEMORY_GROUP_MANAGER_NR_GROUPS - 1)

/**
 * struct mgm_group - Structure to keep track of the number of allocated
 *                    pages per group
 *
 * @size:  The number of allocated small(4KB) pages
 * @lp_size:  The number of allocated large(2MB) pages
 * @insert_pfn: The number of calls to map pages for CPU access.
 * @update_gpu_pte: The number of calls to update GPU page table entries.
 *
 * This structure allows page allocation information to be displayed via
 * debugfs. Display is organized per group with small and large sized pages.
 */
struct mgm_group {
	size_t size;
	size_t lp_size;
	size_t insert_pfn;
	size_t update_gpu_pte;
};

/**
 * struct mgm_groups - Structure for groups of memory group manager
 *
 * @groups: To keep track of the number of allocated pages of all groups
 * @dev: device attached
 * @mgm_debugfs_root: debugfs root directory of memory group manager
 *
 * This structure allows page allocation information to be displayed via
 * debugfs. Display is organized per group with small and large sized pages.
 */
struct mgm_groups {
	struct mgm_group groups[MEMORY_GROUP_MANAGER_NR_GROUPS];
	struct device *dev;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *mgm_debugfs_root;
#endif
};

#if IS_ENABLED(CONFIG_DEBUG_FS)

static int mgm_size_get(void *data, u64 *val)
{
	struct mgm_group *group = data;

	*val = group->size;

	return 0;
}

static int mgm_lp_size_get(void *data, u64 *val)
{
	struct mgm_group *group = data;

	*val = group->lp_size;

	return 0;
}

static int mgm_insert_pfn_get(void *data, u64 *val)
{
	struct mgm_group *group = data;

	*val = group->insert_pfn;

	return 0;
}

static int mgm_update_gpu_pte_get(void *data, u64 *val)
{
	struct mgm_group *group = data;

	*val = group->update_gpu_pte;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_mgm_size, mgm_size_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_mgm_lp_size, mgm_lp_size_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_mgm_insert_pfn, mgm_insert_pfn_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_mgm_update_gpu_pte, mgm_update_gpu_pte_get, NULL, "%llu\n");

static void mgm_term_debugfs(struct mgm_groups *data)
{
	debugfs_remove_recursive(data->mgm_debugfs_root);
}

#define MGM_DEBUGFS_GROUP_NAME_MAX 10
static int mgm_initialize_debugfs(struct mgm_groups *mgm_data)
{
	int i;
	struct dentry *e, *g;
	char debugfs_group_name[MGM_DEBUGFS_GROUP_NAME_MAX];

	/*
	 * Create root directory of memory-group-manager
	 */
	mgm_data->mgm_debugfs_root =
		debugfs_create_dir("physical-memory-group-manager", NULL);
	if (IS_ERR_OR_NULL(mgm_data->mgm_debugfs_root)) {
		dev_err(mgm_data->dev, "fail to create debugfs root directory\n");
		return -ENODEV;
	}

	/*
	 * Create debugfs files per group
	 */
	for (i = 0; i < MEMORY_GROUP_MANAGER_NR_GROUPS; i++) {
		scnprintf(debugfs_group_name, MGM_DEBUGFS_GROUP_NAME_MAX,
				"group_%d", i);
		g = debugfs_create_dir(debugfs_group_name,
				mgm_data->mgm_debugfs_root);
		if (IS_ERR_OR_NULL(g)) {
			dev_err(mgm_data->dev, "fail to create group[%d]\n", i);
			goto remove_debugfs;
		}

		e = debugfs_create_file("size", 0444, g, &mgm_data->groups[i],
				&fops_mgm_size);
		if (IS_ERR_OR_NULL(e)) {
			dev_err(mgm_data->dev, "fail to create size[%d]\n", i);
			goto remove_debugfs;
		}

		e = debugfs_create_file("lp_size", 0444, g,
				&mgm_data->groups[i], &fops_mgm_lp_size);
		if (IS_ERR_OR_NULL(e)) {
			dev_err(mgm_data->dev,
				"fail to create lp_size[%d]\n", i);
			goto remove_debugfs;
		}

		e = debugfs_create_file("insert_pfn", 0444, g,
				&mgm_data->groups[i], &fops_mgm_insert_pfn);
		if (IS_ERR_OR_NULL(e)) {
			dev_err(mgm_data->dev,
				"fail to create insert_pfn[%d]\n", i);
			goto remove_debugfs;
		}

		e = debugfs_create_file("update_gpu_pte", 0444, g,
				&mgm_data->groups[i], &fops_mgm_update_gpu_pte);
		if (IS_ERR_OR_NULL(e)) {
			dev_err(mgm_data->dev,
				"fail to create update_gpu_pte[%d]\n", i);
			goto remove_debugfs;
		}
	}

	return 0;

remove_debugfs:
	mgm_term_debugfs(mgm_data);
	return -ENODEV;
}

#else

static void mgm_term_debugfs(struct mgm_groups *data)
{
}

static int mgm_initialize_debugfs(struct mgm_groups *mgm_data)
{
	return 0;
}

#endif /* CONFIG_DEBUG_FS */

#define ORDER_SMALL_PAGE 0
#define ORDER_LARGE_PAGE 9
static void update_size(struct memory_group_manager_device *mgm_dev, int
		group_id, int order, bool alloc)
{
	struct mgm_groups *data = mgm_dev->data;

	switch (order) {
	case ORDER_SMALL_PAGE:
		if (alloc)
			data->groups[group_id].size++;
		else {
			WARN_ON(data->groups[group_id].size == 0);
			data->groups[group_id].size--;
		}
	break;

	case ORDER_LARGE_PAGE:
		if (alloc)
			data->groups[group_id].lp_size++;
		else {
			WARN_ON(data->groups[group_id].lp_size == 0);
			data->groups[group_id].lp_size--;
		}
	break;

	default:
		dev_err(data->dev, "Unknown order(%d)\n", order);
	break;
	}
}

static struct page *example_mgm_alloc_page(
	struct memory_group_manager_device *mgm_dev, int group_id,
	gfp_t gfp_mask, unsigned int order)
{
	struct mgm_groups *const data = mgm_dev->data;
	struct page *p;

	dev_dbg(data->dev, "%s(mgm_dev=%p, group_id=%d gfp_mask=0x%x order=%u\n",
		__func__, (void *)mgm_dev, group_id, gfp_mask, order);

	if (WARN_ON(group_id < 0) ||
		WARN_ON(group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return NULL;

	p = alloc_pages(gfp_mask, order);

	if (p) {
		update_size(mgm_dev, group_id, order, true);
	} else {
		struct mgm_groups *data = mgm_dev->data;

		dev_err(data->dev, "alloc_pages failed\n");
	}

	return p;
}

static void example_mgm_free_page(
	struct memory_group_manager_device *mgm_dev, int group_id,
	struct page *page, unsigned int order)
{
	struct mgm_groups *const data = mgm_dev->data;

	dev_dbg(data->dev, "%s(mgm_dev=%p, group_id=%d page=%p order=%u\n",
		__func__, (void *)mgm_dev, group_id, (void *)page, order);

	if (WARN_ON(group_id < 0) ||
		WARN_ON(group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return;

	__free_pages(page, order);

	update_size(mgm_dev, group_id, order, false);
}

static int example_mgm_get_import_memory_id(
	struct memory_group_manager_device *mgm_dev,
	struct memory_group_manager_import_data *import_data)
{
	struct mgm_groups *const data = mgm_dev->data;

	dev_dbg(data->dev, "%s(mgm_dev=%p, import_data=%p (type=%d)\n",
		__func__, (void *)mgm_dev, (void *)import_data,
		(int)import_data->type);

	if (!WARN_ON(!import_data)) {
		WARN_ON(!import_data->u.dma_buf);

		WARN_ON(import_data->type !=
				MEMORY_GROUP_MANAGER_IMPORT_TYPE_DMA_BUF);
	}

	return IMPORTED_MEMORY_ID;
}

static u64 example_mgm_update_gpu_pte(
	struct memory_group_manager_device *const mgm_dev, int const group_id,
	int const mmu_level, u64 pte)
{
	struct mgm_groups *const data = mgm_dev->data;
	const u32 pbha_bit_pos = 59; /* bits 62:59 */
	const u32 pbha_bit_mask = 0xf; /* 4-bit */

	dev_dbg(data->dev,
		"%s(mgm_dev=%p, group_id=%d, mmu_level=%d, pte=0x%llx)\n",
		__func__, (void *)mgm_dev, group_id, mmu_level, pte);

	if (WARN_ON(group_id < 0) ||
		WARN_ON(group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return pte;

	pte |= ((u64)group_id & pbha_bit_mask) << pbha_bit_pos;

	data->groups[group_id].update_gpu_pte++;

	return pte;
}

static vm_fault_t example_mgm_vmf_insert_pfn_prot(
	struct memory_group_manager_device *const mgm_dev, int const group_id,
	struct vm_area_struct *const vma, unsigned long const addr,
	unsigned long const pfn, pgprot_t const prot)
{
	struct mgm_groups *const data = mgm_dev->data;
	vm_fault_t fault;

	dev_dbg(data->dev,
		"%s(mgm_dev=%p, group_id=%d, vma=%p, addr=0x%lx, pfn=0x%lx, prot=0x%llx)\n",
		__func__, (void *)mgm_dev, group_id, (void *)vma, addr, pfn,
		(unsigned long long) pgprot_val(prot));

	if (WARN_ON(group_id < 0) ||
		WARN_ON(group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return VM_FAULT_SIGBUS;

	fault = vmf_insert_pfn_prot(vma, addr, pfn, prot);

	if (fault == VM_FAULT_NOPAGE)
		data->groups[group_id].insert_pfn++;
	else
		dev_err(data->dev, "vmf_insert_pfn_prot failed\n");

	return fault;
}

static int mgm_initialize_data(struct mgm_groups *mgm_data)
{
	int i;

	for (i = 0; i < MEMORY_GROUP_MANAGER_NR_GROUPS; i++) {
		mgm_data->groups[i].size = 0;
		mgm_data->groups[i].lp_size = 0;
		mgm_data->groups[i].insert_pfn = 0;
		mgm_data->groups[i].update_gpu_pte = 0;
	}

	return mgm_initialize_debugfs(mgm_data);
}

static void mgm_term_data(struct mgm_groups *data)
{
	int i;

	for (i = 0; i < MEMORY_GROUP_MANAGER_NR_GROUPS; i++) {
		if (data->groups[i].size != 0)
			dev_warn(data->dev,
				"%zu 0-order pages in group(%d) leaked\n",
				data->groups[i].size, i);
		if (data->groups[i].lp_size != 0)
			dev_warn(data->dev,
				"%zu 9 order pages in group(%d) leaked\n",
				data->groups[i].lp_size, i);
	}

	mgm_term_debugfs(data);
}

static int memory_group_manager_probe(struct platform_device *pdev)
{
	struct memory_group_manager_device *mgm_dev;
	struct mgm_groups *mgm_data;

	mgm_dev = kzalloc(sizeof(*mgm_dev), GFP_KERNEL);
	if (!mgm_dev)
		return -ENOMEM;

	mgm_dev->owner = THIS_MODULE;
	mgm_dev->ops.mgm_alloc_page = example_mgm_alloc_page;
	mgm_dev->ops.mgm_free_page = example_mgm_free_page;
	mgm_dev->ops.mgm_get_import_memory_id =
			example_mgm_get_import_memory_id;
	mgm_dev->ops.mgm_vmf_insert_pfn_prot = example_mgm_vmf_insert_pfn_prot;
	mgm_dev->ops.mgm_update_gpu_pte = example_mgm_update_gpu_pte;

	mgm_data = kzalloc(sizeof(*mgm_data), GFP_KERNEL);
	if (!mgm_data) {
		kfree(mgm_dev);
		return -ENOMEM;
	}

	mgm_dev->data = mgm_data;
	mgm_data->dev = &pdev->dev;

	if (mgm_initialize_data(mgm_data)) {
		kfree(mgm_data);
		kfree(mgm_dev);
		return -ENOENT;
	}

	platform_set_drvdata(pdev, mgm_dev);
	dev_info(&pdev->dev, "Memory group manager probed successfully\n");

	return 0;
}

static int memory_group_manager_remove(struct platform_device *pdev)
{
	struct memory_group_manager_device *mgm_dev =
		platform_get_drvdata(pdev);
	struct mgm_groups *mgm_data = mgm_dev->data;

	mgm_term_data(mgm_data);
	kfree(mgm_data);

	kfree(mgm_dev);

	dev_info(&pdev->dev, "Memory group manager removed successfully\n");

	return 0;
}

static const struct of_device_id memory_group_manager_dt_ids[] = {
	{ .compatible = "arm,physical-memory-group-manager" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, memory_group_manager_dt_ids);

static struct platform_driver memory_group_manager_driver = {
	.probe = memory_group_manager_probe,
	.remove = memory_group_manager_remove,
	.driver = {
		.name = "physical-memory-group-manager",
		.of_match_table = of_match_ptr(memory_group_manager_dt_ids),
		/*
		 * Prevent the mgm_dev from being unbound and freed, as other's
		 * may have pointers to it and would get confused, or crash, if
		 * it suddenly disappear.
		 */
		.suppress_bind_attrs = true,
	}
};

module_platform_driver(memory_group_manager_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ARM Ltd.");
MODULE_VERSION("1.0");
