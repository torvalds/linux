// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/dma-mapping-fast.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/qcom-iommu-util.h>
#include "qcom-dma-iommu-generic.h"
#include "qcom-io-pgtable.h"
#include "qcom-io-pgtable-alloc.h"

struct qcom_iommu_range_prop_cb_data {
	int (*range_prop_entry_cb_fn)(const __be32 *p, int naddr, int nsize, void *arg);
	void *arg;
};

struct iova_range {
	u64 base;
	u64 end;
};

struct device_node *qcom_iommu_group_parse_phandle(struct device *dev)
{
	struct device_node *np;

	if (!dev->of_node)
		return NULL;

	np = of_parse_phandle(dev->of_node, "qcom,iommu-group", 0);
	return np ? np : dev->of_node;
}

static int of_property_walk_each_entry(struct device *dev, const char *propname,
				       struct qcom_iommu_range_prop_cb_data *cb_data)
{
	struct device_node *np;
	const __be32 *p, *property_end;
	int ret, len, naddr, nsize;

	np = qcom_iommu_group_parse_phandle(dev);
	if (!np)
		return -EINVAL;

	p = of_get_property(np, propname, &len);
	if (!p)
		return -ENODEV;

	len /= sizeof(u32);
	naddr = of_n_addr_cells(np);
	nsize = of_n_size_cells(np);
	if (!naddr || !nsize || len % (naddr + nsize)) {
		dev_err(dev, "%s Invalid length %d. Address cells %d. Size cells %d\n",
			propname, len, naddr, nsize);
		return -EINVAL;
	}
	property_end = p + len;

	while (p < property_end) {
		ret = cb_data->range_prop_entry_cb_fn(p, naddr, nsize, cb_data->arg);
		if (ret)
			return ret;

		p += naddr + nsize;
	}

	return 0;
}

static bool check_overlap(struct iommu_resv_region *region, u64 start, u64 end)
{
	u64 region_end = region->start + region->length - 1;

	return end >= region->start && start <= region_end;
}

static int insert_range(const __be32 *p, int naddr, int nsize, void *arg)
{
	struct list_head *head = arg;
	struct iommu_resv_region *region, *new;
	u64 start = of_read_number(p, naddr);
	u64 end = start + of_read_number(p + naddr, nsize) - 1;

	list_for_each_entry(region, head, list) {
		if (check_overlap(region, start, end))
			return -EINVAL;

		if (start < region->start)
			break;
	}

	new = iommu_alloc_resv_region(start, end - start + 1,
					0, IOMMU_RESV_RESERVED);
	if (!new)
		return -ENOMEM;
	list_add_tail(&new->list, &region->list);
	return 0;
}

/*
 * Returns a sorted list of all regions described by the
 * "qcom,iommu-dma-addr-pool" property.
 *
 * Caller is responsible for freeing the entries on the list via
 * generic_iommu_put_resv_regions
 */
int qcom_iommu_generate_dma_regions(struct device *dev,
		struct list_head *head)
{
	struct qcom_iommu_range_prop_cb_data insert_range_cb_data = {
		.range_prop_entry_cb_fn = insert_range,
		.arg = head,
	};

	return of_property_walk_each_entry(dev, "qcom,iommu-dma-addr-pool",
					   &insert_range_cb_data);
}
EXPORT_SYMBOL(qcom_iommu_generate_dma_regions);

static int invert_regions(struct list_head *head, struct list_head *inverted)
{
	struct iommu_resv_region *prev, *curr, *new;
	phys_addr_t rsv_start;
	size_t rsv_size;
	int ret = 0;

	/*
	 * Since its not possible to express start 0, size 1<<64 return
	 * an error instead. Also an iova allocator without any iovas doesn't
	 * make sense.
	 */
	if (list_empty(head))
		return -EINVAL;

	/*
	 * Handle case where there is a non-zero sized area between
	 * iommu_resv_regions A & B.
	 */
	prev = NULL;
	list_for_each_entry(curr, head, list) {
		if (!prev)
			goto next;

		rsv_start = prev->start + prev->length;
		rsv_size = curr->start - rsv_start;
		if (!rsv_size)
			goto next;

		new = iommu_alloc_resv_region(rsv_start, rsv_size,
						0, IOMMU_RESV_RESERVED);
		if (!new) {
			ret = -ENOMEM;
			goto out_err;
		}
		list_add_tail(&new->list, inverted);
next:
		prev = curr;
	}

	/* Now handle the beginning */
	curr = list_first_entry(head, struct iommu_resv_region, list);
	rsv_start = 0;
	rsv_size = curr->start;
	if (rsv_size) {
		new = iommu_alloc_resv_region(rsv_start, rsv_size,
						0, IOMMU_RESV_RESERVED);
		if (!new) {
			ret = -ENOMEM;
			goto out_err;
		}
		list_add(&new->list, inverted);
	}

	/* Handle the end - checking for overflow */
	rsv_start = prev->start + prev->length;
	rsv_size = -rsv_start;

	if (rsv_size && (U64_MAX - prev->start > prev->length)) {
		new = iommu_alloc_resv_region(rsv_start, rsv_size,
						0, IOMMU_RESV_RESERVED);
		if (!new) {
			ret = -ENOMEM;
			goto out_err;
		}
		list_add_tail(&new->list, inverted);
	}

	return 0;

out_err:
	list_for_each_entry_safe(curr, prev, inverted, list)
		kfree(curr);
	return ret;
}

/* Used by iommu drivers to generate reserved regions for qcom,iommu-dma-addr-pool property */
void qcom_iommu_generate_resv_regions(struct device *dev,
				      struct list_head *head)
{
	struct iommu_resv_region *region;
	LIST_HEAD(dma_regions);
	LIST_HEAD(resv_regions);
	int ret;

	ret = qcom_iommu_generate_dma_regions(dev, &dma_regions);
	if (ret)
		return;

	ret = invert_regions(&dma_regions, &resv_regions);
	generic_iommu_put_resv_regions(dev, &dma_regions);
	if (ret)
		return;

	list_for_each_entry(region, &resv_regions, list) {
		dev_dbg(dev, "Reserved region %llx-%llx\n",
			(u64)region->start,
			(u64)(region->start + region->length - 1));
	}

	list_splice(&resv_regions, head);
}
EXPORT_SYMBOL(qcom_iommu_generate_resv_regions);

void qcom_iommu_get_resv_regions(struct device *dev, struct list_head *list)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (ops && ops->get_resv_regions)
		ops->get_resv_regions(dev, list);
}
EXPORT_SYMBOL(qcom_iommu_get_resv_regions);

void qcom_iommu_put_resv_regions(struct device *dev, struct list_head *list)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	if (ops && ops->put_resv_regions)
		ops->put_resv_regions(dev, list);
}
EXPORT_SYMBOL(qcom_iommu_put_resv_regions);

static int get_addr_range(const __be32 *p, int naddr, int nsize, void *arg)
{

	u64 start = of_read_number(p, naddr);
	u64 end = start + of_read_number(p + naddr, nsize) - 1;
	struct iova_range *range = arg;

	if (start >= SZ_4G || end >= SZ_4G) {
		pr_err("fastmap does not support IOVAs >= 4 GB\n");
		return -EINVAL;
	}

	range->base = min_not_zero(range->base, start);
	range->end = max(range->end, end);

	return 0;
}

int qcom_iommu_get_fast_iova_range(struct device *dev, dma_addr_t *ret_iova_base,
				   dma_addr_t *ret_iova_end)
{
	struct iova_range dma_range = {};
	struct iova_range geometry_range = {};
	struct qcom_iommu_range_prop_cb_data get_addr_range_cb_data = {
		.range_prop_entry_cb_fn = get_addr_range,
	};
	int ret;

	if (!dev || !ret_iova_base || !ret_iova_end)
		return -EINVAL;

	get_addr_range_cb_data.arg = &dma_range;
	ret = of_property_walk_each_entry(dev, "qcom,iommu-dma-addr-pool",
					  &get_addr_range_cb_data);
	if (ret == -ENODEV) {
		dma_range.base = 0;
		dma_range.end = SZ_4G - 1;
	} else if (ret) {
		return ret;
	}

	get_addr_range_cb_data.arg = &geometry_range;
	ret = of_property_walk_each_entry(dev, "qcom,iommu-geometry",
					  &get_addr_range_cb_data);
	if (ret == -ENODEV) {
		geometry_range.base = 0;
		geometry_range.end = SZ_4G - 1;
	} else if (ret) {
		return ret;
	}

	*ret_iova_base = min(geometry_range.base, dma_range.base);
	*ret_iova_end =  max(geometry_range.end, dma_range.end);
	return 0;
}
EXPORT_SYMBOL(qcom_iommu_get_fast_iova_range);

phys_addr_t qcom_iommu_iova_to_phys_hard(struct iommu_domain *domain,
				    struct qcom_iommu_atos_txn *txn)
{
	struct qcom_iommu_ops *ops = to_qcom_iommu_ops(domain->ops);

	if (unlikely(ops->iova_to_phys_hard == NULL))
		return 0;

	return ops->iova_to_phys_hard(domain, txn);
}
EXPORT_SYMBOL(qcom_iommu_iova_to_phys_hard);

int qcom_iommu_sid_switch(struct device *dev, enum sid_switch_direction dir)
{
	struct qcom_iommu_ops *ops;
	struct iommu_domain *domain;

	domain = iommu_get_domain_for_dev(dev);
	if (!domain)
		return -EINVAL;

	ops = to_qcom_iommu_ops(domain->ops);
	if (unlikely(ops->sid_switch == NULL))
		return -EINVAL;

	return ops->sid_switch(dev, dir);
}
EXPORT_SYMBOL(qcom_iommu_sid_switch);

int qcom_iommu_get_fault_ids(struct iommu_domain *domain,
			     struct qcom_iommu_fault_ids *f_ids)
{
	struct qcom_iommu_ops *ops = to_qcom_iommu_ops(domain->ops);

	if (unlikely(ops->get_fault_ids == NULL))
		return -EINVAL;

	return ops->get_fault_ids(domain, f_ids);
}
EXPORT_SYMBOL(qcom_iommu_get_fault_ids);

int qcom_iommu_get_msi_size(struct device *dev, u32 *msi_size)
{
	struct device_node *np = qcom_iommu_group_parse_phandle(dev);

	if (!np)
		return -EINVAL;

	return of_property_read_u32(np, "qcom,iommu-msi-size", msi_size);
}

int qcom_iommu_get_context_bank_nr(struct iommu_domain *domain)
{
	struct qcom_iommu_ops *ops = to_qcom_iommu_ops(domain->ops);

	if (unlikely(ops->get_context_bank_nr == NULL))
		return -EINVAL;

	return ops->get_context_bank_nr(domain);
}
EXPORT_SYMBOL(qcom_iommu_get_context_bank_nr);

int qcom_iommu_set_secure_vmid(struct iommu_domain *domain, enum vmid vmid)
{
	struct qcom_iommu_ops *ops = to_qcom_iommu_ops(domain->ops);

	if (unlikely(ops->set_secure_vmid == NULL))
		return -EINVAL;

	return ops->set_secure_vmid(domain, vmid);
}
EXPORT_SYMBOL(qcom_iommu_set_secure_vmid);

int qcom_iommu_set_fault_model(struct iommu_domain *domain, int fault_model)
{
	struct qcom_iommu_ops *ops = to_qcom_iommu_ops(domain->ops);

	if (unlikely(ops->set_fault_model == NULL))
		return -EINVAL;
	else if (fault_model & ~(QCOM_IOMMU_FAULT_MODEL_NON_FATAL |
				 QCOM_IOMMU_FAULT_MODEL_NO_CFRE |
				 QCOM_IOMMU_FAULT_MODEL_NO_STALL |
				 QCOM_IOMMU_FAULT_MODEL_HUPCF))
		return -EINVAL;

	return ops->set_fault_model(domain, fault_model);
}
EXPORT_SYMBOL(qcom_iommu_set_fault_model);

int qcom_iommu_enable_s1_translation(struct iommu_domain *domain)
{
	struct qcom_iommu_ops *ops = to_qcom_iommu_ops(domain->ops);

	if (unlikely(ops->enable_s1_translation == NULL))
		return -EINVAL;

	return ops->enable_s1_translation(domain);
}
EXPORT_SYMBOL(qcom_iommu_enable_s1_translation);

int qcom_iommu_get_mappings_configuration(struct iommu_domain *domain)
{
	struct qcom_iommu_ops *ops = to_qcom_iommu_ops(domain->ops);

	if (unlikely(ops->get_mappings_configuration == NULL))
		return -EINVAL;

	return ops->get_mappings_configuration(domain);
}
EXPORT_SYMBOL(qcom_iommu_get_mappings_configuration);

struct io_pgtable_ops *qcom_alloc_io_pgtable_ops(enum io_pgtable_fmt fmt,
				struct qcom_io_pgtable_info *pgtbl_info,
				void *cookie)
{
	struct io_pgtable *iop;
	const struct io_pgtable_init_fns *fns;
	struct io_pgtable_cfg *cfg = &pgtbl_info->cfg;

	if (fmt < IO_PGTABLE_NUM_FMTS)
		return alloc_io_pgtable_ops(fmt, cfg, cookie);
#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST
	else if (fmt == ARM_V8L_FAST)
		fns = &io_pgtable_av8l_fast_init_fns;
#endif
#ifdef CONFIG_IOMMU_IO_PGTABLE_LPAE
	else if (fmt == QCOM_ARM_64_LPAE_S1)
		fns = &qcom_io_pgtable_arm_64_lpae_s1_init_fns;
#endif
	else {
		pr_err("Invalid io-pgtable fmt %u\n", fmt);
		return NULL;
	}

	iop = fns->alloc(cfg, cookie);
	if (!iop)
		return NULL;

	iop->fmt	= fmt;
	iop->cookie	= cookie;
	iop->cfg	= *cfg;

	return &iop->ops;
}
EXPORT_SYMBOL(qcom_alloc_io_pgtable_ops);

void qcom_free_io_pgtable_ops(struct io_pgtable_ops *ops)
{
	struct io_pgtable *iop;
	enum io_pgtable_fmt fmt;
	const struct io_pgtable_init_fns *fns;

	if (!ops)
		return;

	iop = io_pgtable_ops_to_pgtable(ops);
	fmt = iop->fmt;
	if (fmt < IO_PGTABLE_NUM_FMTS)
		return free_io_pgtable_ops(ops);
#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST
	else if (fmt == ARM_V8L_FAST)
		fns = &io_pgtable_av8l_fast_init_fns;
#endif
#ifdef CONFIG_IOMMU_IO_PGTABLE_LPAE
	else if (fmt == QCOM_ARM_64_LPAE_S1)
		fns = &qcom_io_pgtable_arm_64_lpae_s1_init_fns;
#endif
	else {
		pr_err("Invalid io-pgtable fmt %u\n", fmt);
		return;
	}

	io_pgtable_tlb_flush_all(iop);
	fns->free(iop);
}
EXPORT_SYMBOL(qcom_free_io_pgtable_ops);

/*
 * These tables must have the same length.
 * It is allowed to have a NULL exitcall corresponding to a non-NULL initcall.
 */
static initcall_t init_table[] __initdata = {
	dma_mapping_fast_init,
	qcom_dma_iommu_generic_driver_init,
	qcom_arm_lpae_do_selftests,
	qcom_io_pgtable_alloc_init,
	NULL
};

static exitcall_t exit_table[] = {
	NULL, /* dma_mapping_fast_exit */
	qcom_dma_iommu_generic_driver_exit,
	NULL, /*qcom_arm_lpae_do_selftests */
	qcom_io_pgtable_alloc_exit,
	NULL,
};

static int __init qcom_iommu_util_init(void)
{
	initcall_t *init_fn;
	exitcall_t *exit_fn;
	int ret;

	if (ARRAY_SIZE(init_table) != ARRAY_SIZE(exit_table)) {
		pr_err("qcom-iommu-util: Invalid initcall/exitcall table\n");
		return -EINVAL;
	}

	for (init_fn = init_table; *init_fn; init_fn++) {
		ret = (**init_fn)();
		if (ret) {
			pr_err("%ps returned %d\n", *init_fn, ret);
			goto out_undo;
		}
	}

	return 0;

out_undo:
	exit_fn = exit_table + (init_fn - init_table);
	for (exit_fn--; exit_fn >= exit_table; exit_fn--) {
		if (!*exit_fn)
			continue;
		(**exit_fn)();
	}
	return ret;
}
module_init(qcom_iommu_util_init);

MODULE_LICENSE("GPL v2");
