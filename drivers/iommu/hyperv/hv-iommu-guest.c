// SPDX-License-Identifier: GPL-2.0

/*
 * Hyper-V para-virtualized IOMMU driver for Linux guests.
 *
 * Copyright (C) 2024-2026 Microsoft, Inc.
 */

#define pr_fmt(fmt) "Hyper-V pvIOMMU: " fmt
#define dev_fmt(fmt) pr_fmt(fmt)

#include <linux/iommu.h>
#include <linux/pci.h>
#include <linux/dma-map-ops.h>
#include <linux/generic_pt/iommu.h>
#include <linux/pci-ats.h>

#include <asm/iommu.h>
#include <asm/hypervisor.h>
#include <asm/mshyperv.h>

#include "iommu.h"
#include "../iommu-pages.h"

static struct hv_iommu_dev *hv_iommu_device;

/*
 * Identity and blocking domains are static singletons: identity is a 1:1
 * passthrough with no page table, blocking rejects all DMA. Neither holds
 * per-IOMMU state, so one instance suffices even with multiple vIOMMUs.
 */
static const struct iommu_domain_ops hv_iommu_identity_domain_ops;
static const struct iommu_domain_ops hv_iommu_blocking_domain_ops;
static struct iommu_ops hv_iommu_ops;

static struct hv_iommu_domain hv_identity_domain = {
	.domain = {
		.type	= IOMMU_DOMAIN_IDENTITY,
		.ops	= &hv_iommu_identity_domain_ops,
		.owner	= &hv_iommu_ops,
	},
};

static struct hv_iommu_domain hv_blocking_domain = {
	.domain = {
		.type	= IOMMU_DOMAIN_BLOCKED,
		.ops	= &hv_iommu_blocking_domain_ops,
		.owner	= &hv_iommu_ops,
	},
};

static inline bool hv_iommu_present(u64 cap)
{
	return cap & HV_IOMMU_CAP_PRESENT;
}

static inline bool hv_iommu_s1_domain_supported(u64 cap)
{
	return cap & HV_IOMMU_CAP_S1;
}

static inline bool hv_iommu_5lvl_supported(u64 cap)
{
	return cap & HV_IOMMU_CAP_S1_5LVL;
}

static inline bool hv_iommu_ats_supported(u64 cap)
{
	return cap & HV_IOMMU_CAP_ATS;
}

static int hv_create_device_domain(struct hv_iommu_domain *hv_domain)
{
	int ret;
	u64 status;
	unsigned long flags;
	struct hv_input_create_device_domain *input;

	ret = ida_alloc_range(&hv_iommu_device->domain_ids,
			      hv_iommu_device->first_domain,
			      hv_iommu_device->last_domain, GFP_KERNEL);
	if (ret < 0)
		return ret;

	hv_domain->device_domain.partition_id = HV_PARTITION_ID_SELF;
	hv_domain->device_domain.domain_id.type = HV_DEVICE_DOMAIN_TYPE_S1;
	hv_domain->device_domain.domain_id.id = ret;
	hv_domain->hv_iommu = hv_iommu_device;

	local_irq_save(flags);

	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	memset(input, 0, sizeof(*input));
	input->device_domain = hv_domain->device_domain;
	input->create_device_domain_flags.forward_progress_required = 1;
	input->create_device_domain_flags.inherit_owning_vtl = 0;
	status = hv_do_hypercall(HVCALL_CREATE_DEVICE_DOMAIN, input, NULL);

	local_irq_restore(flags);

	if (!hv_result_success(status)) {
		hv_status_err(status, "HVCALL_CREATE_DEVICE_DOMAIN failed\n");
		ida_free(&hv_iommu_device->domain_ids,
			 hv_domain->device_domain.domain_id.id);
	}

	return hv_result_to_errno(status);
}

static void hv_delete_device_domain(struct hv_iommu_domain *hv_domain)
{
	u64 status;
	unsigned long flags;
	struct hv_input_delete_device_domain *input;

	local_irq_save(flags);

	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	memset(input, 0, sizeof(*input));
	input->device_domain = hv_domain->device_domain;
	status = hv_do_hypercall(HVCALL_DELETE_DEVICE_DOMAIN, input, NULL);

	local_irq_restore(flags);

	if (!hv_result_success(status))
		hv_status_err(status, "HVCALL_DELETE_DEVICE_DOMAIN failed\n");

	ida_free(&hv_domain->hv_iommu->domain_ids,
		 hv_domain->device_domain.domain_id.id);
}

static int
hv_configure_device_domain(struct hv_iommu_domain *hv_domain,
			   const struct hv_device_domain_settings *settings)
{
	u64 status;
	unsigned long flags;
	struct hv_input_configure_device_domain *input;

	local_irq_save(flags);

	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	memset(input, 0, sizeof(*input));
	input->device_domain = hv_domain->device_domain;
	input->settings = *settings;
	status = hv_do_hypercall(HVCALL_CONFIGURE_DEVICE_DOMAIN, input, NULL);

	local_irq_restore(flags);

	if (!hv_result_success(status))
		hv_status_err(status,
			      "HVCALL_CONFIGURE_DEVICE_DOMAIN failed\n");

	return hv_result_to_errno(status);
}

static int
hv_create_configure_device_domain(struct hv_iommu_domain *hv_domain,
				  const struct hv_device_domain_settings *settings)
{
	int ret;

	ret = hv_create_device_domain(hv_domain);
	if (ret)
		return ret;

	ret = hv_configure_device_domain(hv_domain, settings);
	if (ret)
		hv_delete_device_domain(hv_domain);

	return ret;
}

static bool hv_iommu_capable(struct device *dev, enum iommu_cap cap)
{
	return cap == IOMMU_CAP_CACHE_COHERENCY;
}

static void hv_flush_device_domain(struct hv_iommu_domain *hv_domain)
{
	u64 status;
	unsigned long flags;
	struct hv_input_flush_device_domain *input;

	local_irq_save(flags);

	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	memset(input, 0, sizeof(*input));
	input->device_domain = hv_domain->device_domain;
	status = hv_do_hypercall(HVCALL_FLUSH_DEVICE_DOMAIN, input, NULL);

	local_irq_restore(flags);

	WARN_ONCE(!hv_result_success(status),
		  "HVCALL_FLUSH_DEVICE_DOMAIN failed: %#llx (%s)\n",
		  status, hv_result_to_string(status));
}

static int hv_iommu_attach_dev(struct iommu_domain *domain, struct device *dev,
			       struct iommu_domain *old)
{
	u64 status;
	u32 prefix;
	unsigned long flags;
	struct pci_dev *pdev;
	struct hv_input_attach_device_domain *input;
	struct hv_iommu_domain *hv_domain = to_hv_iommu_domain(domain);
	int pci_domain;
	int ret;

	pdev = to_pci_dev(dev);
	dev_dbg(dev, "attaching to domain %d\n",
		hv_domain->device_domain.domain_id.id);

	pci_domain = pci_domain_nr(pdev->bus);
	ret = hv_pci_lookup_dev_id(pci_domain, &prefix);
	if (ret) {
		dev_err(dev,
			"no logical device ID registered for PCI domain %04x\n",
			pci_domain);
		return ret;
	}

	local_irq_save(flags);

	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	memset(input, 0, sizeof(*input));
	input->device_domain = hv_domain->device_domain;
	input->device_id.as_uint64 = (u64)prefix | PCI_FUNC(pdev->devfn);
	status = hv_do_hypercall(HVCALL_ATTACH_DEVICE_DOMAIN, input, NULL);

	local_irq_restore(flags);

	if (!hv_result_success(status) &&
	    hv_result(status) != HV_STATUS_DEVICE_ALREADY_IN_DOMAIN) {
		hv_status_err(status, "HVCALL_ATTACH_DEVICE_DOMAIN failed\n");
		return hv_result_to_errno(status);
	}

	if (domain != &hv_blocking_domain.domain &&
	    !pdev->ats_enabled &&
	    hv_iommu_ats_supported(hv_domain->hv_iommu->cap) &&
	    pci_ats_supported(pdev))
		pci_enable_ats(pdev, PAGE_SHIFT);

	return 0;
}

static int hv_iommu_blocking_attach_dev(struct iommu_domain *domain,
					struct device *dev,
					struct iommu_domain *old)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	if (pdev->ats_enabled)
		pci_disable_ats(pdev);

	ret = hv_iommu_attach_dev(domain, dev, old);

	/*
	 * Attaching to the blocking domain only asks the hypervisor to
	 * disable translation and IOPF for the device, so it cannot fail
	 * unless there is a driver or hypervisor bug. Return the hypercall
	 * status rather than 0 so that a failure on the DMA ownership claim
	 * path (VFIO/iommufd) fails the claim instead of leaving the device
	 * unblocked. WARN since such a failure indicates a bug.
	 */
	WARN_ON(ret);
	return ret;
}

static int
hv_iommu_get_dev_property(struct device *dev, u32 code,
			  struct hv_output_get_logical_device_property *prop)
{
	u64 status;
	u32 prefix;
	unsigned long flags;
	int ret;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct hv_input_get_logical_device_property *input;
	struct hv_output_get_logical_device_property *output;

	ret = hv_pci_lookup_dev_id(pci_domain_nr(pdev->bus), &prefix);
	if (ret)
		return ret;

	local_irq_save(flags);

	/*
	 * The combined input and output fit in the per-CPU input page,
	 * so no separate output page is needed.
	 */
	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	output = (struct hv_output_get_logical_device_property *)(input + 1);
	memset(input, 0, sizeof(*input));
	input->partition_id = HV_PARTITION_ID_SELF;
	input->logical_device_id = (u64)prefix | PCI_FUNC(pdev->devfn);
	input->code = code;
	status = hv_do_hypercall(HVCALL_GET_LOGICAL_DEVICE_PROPERTY,
				 input, output);
	*prop = *output;

	local_irq_restore(flags);

	if (!hv_result_success(status))
		hv_status_err(status,
			      "HVCALL_GET_LOGICAL_DEVICE_PROPERTY failed\n");

	return hv_result_to_errno(status);
}

static struct iommu_device *hv_iommu_probe_device(struct device *dev)
{
	struct hv_output_get_logical_device_property property = {0};
	int ret;

	if (!dev_is_pci(dev))
		return ERR_PTR(-ENODEV);

	ret = hv_iommu_get_dev_property(dev, HV_LOGICAL_DEVICE_PROPERTY_PVIOMMU,
					&property);
	if (ret || !(property.device_iommu & HV_DEVICE_IOMMU_ENABLED))
		return ERR_PTR(-ENODEV);

	return &hv_iommu_device->iommu;
}

static struct iommu_group *hv_iommu_device_group(struct device *dev)
{
	if (!dev_is_pci(dev))
		return ERR_PTR(-ENODEV);

	return pci_device_group(dev);
}

static int __init hv_initialize_static_domains(void)
{
	/*
	 * Clearing translation_enabled bypasses stage-1 translation, so DMA
	 * addresses are used directly as GPAs. Hyper-V requires paging and
	 * blocked domains to keep translation_enabled set.
	 */
	const struct hv_device_domain_settings identity_settings = {
		.flags.translation_enabled = 0,
	};
	const struct hv_device_domain_settings blocked_settings = {
		.flags = {
			.translation_enabled = 1,
			.blocked = 1,
		},
	};
	int ret;

	/* Default stage-1 identity domain */
	ret = hv_create_configure_device_domain(&hv_identity_domain,
						&identity_settings);
	if (ret)
		return ret;

	/* Default stage-1 blocked domain */
	ret = hv_create_configure_device_domain(&hv_blocking_domain,
						&blocked_settings);
	if (ret)
		goto delete_identity_domain;

	return 0;

delete_identity_domain:
	hv_delete_device_domain(&hv_identity_domain);
	return ret;
}

static void hv_iommu_get_resv_regions(struct device *dev,
				      struct list_head *head)
{
	iommu_add_resv_x86_msi_region(head);
}

static void hv_iommu_flush_iotlb_all(struct iommu_domain *domain)
{
	hv_flush_device_domain(to_hv_iommu_domain(domain));
}

/*
 * Calculate the minimal power-of-two aligned range that covers [start, end]
 * (end is inclusive). Returns a single (page_number, page_mask_shift)
 * descriptor that may over-flush when the range is not naturally aligned.
 */
static void
hv_iommu_calc_flush_range(unsigned long start, unsigned long end,
			  union hv_iommu_flush_va *va)
{
	unsigned long aligned_start;
	unsigned int sz_lg2;

	sz_lg2 = max(fls_long(start ^ end), HV_HYP_PAGE_SHIFT);

	/*
	 * A valid IOVA range shall not span bit 63. Use the maximum mask
	 * so the host can safely perform a full flush.
	 */
	if (WARN_ON_ONCE(sz_lg2 == BITS_PER_LONG))
		aligned_start = 0;
	else
		aligned_start =
			start & GENMASK(BITS_PER_LONG - 1, sz_lg2);

	va->page_number = aligned_start >> HV_HYP_PAGE_SHIFT;
	va->page_mask_shift = sz_lg2 - HV_HYP_PAGE_SHIFT;
}

static void hv_flush_device_domain_list(struct hv_iommu_domain *hv_domain,
					struct iommu_iotlb_gather *iotlb_gather)
{
	u64 status;
	unsigned long flags;
	struct hv_input_flush_device_domain_list *input;

	local_irq_save(flags);

	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	/* Clear the fixed header and the single range entry. */
	memset(input, 0, struct_size(input, iova_list, 1));

	input->device_domain = hv_domain->device_domain;
	input->flags |= HV_FLUSH_DEVICE_DOMAIN_LIST_IOMMU_FORMAT;
	hv_iommu_calc_flush_range(iotlb_gather->start, iotlb_gather->end,
				  &input->iova_list[0]);

	status = hv_do_rep_hypercall(HVCALL_FLUSH_DEVICE_DOMAIN_LIST,
				     1, 0, input, NULL);

	if (WARN_ONCE(!hv_result_success(status),
		      "HVCALL_FLUSH_DEVICE_DOMAIN_LIST failed: %#llx (%s)\n",
		      status, hv_result_to_string(status))) {
		/* Page-selective flush failed, fall back to full flush. */
		struct hv_input_flush_device_domain *flush_all = (void *)input;

		memset(flush_all, 0, sizeof(*flush_all));
		flush_all->device_domain = hv_domain->device_domain;
		status = hv_do_hypercall(HVCALL_FLUSH_DEVICE_DOMAIN,
					 flush_all, NULL);
		WARN(!hv_result_success(status),
		     "HVCALL_FLUSH_DEVICE_DOMAIN fallback also failed: %#llx (%s)\n",
		     status, hv_result_to_string(status));
	}

	local_irq_restore(flags);
}

static void hv_iommu_iotlb_sync(struct iommu_domain *domain,
				struct iommu_iotlb_gather *iotlb_gather)
{
	hv_flush_device_domain_list(to_hv_iommu_domain(domain), iotlb_gather);

	/*
	 * The hypercall also invalidates non-leaf page-walk caches, so
	 * detached page-table pages can now be freed.
	 */
	iommu_put_pages_list(&iotlb_gather->freelist);
}

static void hv_iommu_paging_domain_free(struct iommu_domain *domain)
{
	struct hv_iommu_domain *hv_domain = to_hv_iommu_domain(domain);

	/* Free all remaining mappings */
	pt_iommu_deinit(&hv_domain->pt_iommu);

	hv_delete_device_domain(hv_domain);

	kfree(hv_domain);
}

static const struct iommu_domain_ops hv_iommu_identity_domain_ops = {
	.attach_dev	= hv_iommu_attach_dev,
};

static const struct iommu_domain_ops hv_iommu_blocking_domain_ops = {
	.attach_dev	= hv_iommu_blocking_attach_dev,
};

static const struct iommu_domain_ops hv_iommu_paging_domain_ops = {
	.attach_dev	= hv_iommu_attach_dev,
	IOMMU_PT_DOMAIN_OPS(x86_64),
	.flush_iotlb_all = hv_iommu_flush_iotlb_all,
	.iotlb_sync = hv_iommu_iotlb_sync,
	.free = hv_iommu_paging_domain_free,
};

static struct iommu_domain *hv_iommu_domain_alloc_paging(struct device *dev)
{
	int ret;
	struct hv_iommu_domain *hv_domain;
	struct pt_iommu_x86_64_hw_info pt_info;
	struct pt_iommu_x86_64_cfg cfg = {};
	struct hv_device_domain_settings settings = {
		.flags = {
			.translation_enabled = 1,
		},
	};

	hv_domain = kzalloc_obj(*hv_domain, GFP_KERNEL);
	if (!hv_domain)
		return ERR_PTR(-ENOMEM);

	hv_domain->pt_iommu.nid = dev_to_node(dev);

	cfg.common.hw_max_vasz_lg2 = hv_iommu_device->max_iova_width;
	cfg.common.hw_max_oasz_lg2 = 52;
	cfg.common.features |= BIT(PT_FEAT_FLUSH_RANGE);
	/*
	 * Hyper-V S1 domains use a 4-level root for IOVA widths up to
	 * 48 bits. A 5-level root is used only for wider apertures when
	 * HV_IOMMU_CAP_S1_5LVL is advertised.
	 */
	cfg.top_level = (hv_iommu_device->max_iova_width > 48) ? 4 : 3;

	ret = pt_iommu_x86_64_init(&hv_domain->pt_iommu_x86_64, &cfg, GFP_KERNEL);
	if (ret)
		goto err_free;

	/* Constrain to page sizes the hypervisor supports */
	hv_domain->domain.pgsize_bitmap &= hv_iommu_device->pgsize_bitmap;

	hv_domain->domain.ops = &hv_iommu_paging_domain_ops;

	pt_iommu_x86_64_hw_info(&hv_domain->pt_iommu_x86_64, &pt_info);
	settings.page_table_root = pt_info.gcr3_pt;
	settings.flags.first_stage_paging_mode = pt_info.levels == 5;

	ret = hv_create_configure_device_domain(hv_domain, &settings);
	if (ret)
		goto err_pt_deinit;

	return &hv_domain->domain;

err_pt_deinit:
	pt_iommu_deinit(&hv_domain->pt_iommu);
err_free:
	kfree(hv_domain);
	return ERR_PTR(ret);
}

static struct iommu_ops hv_iommu_ops = {
	.capable		  = hv_iommu_capable,
	.domain_alloc_paging	  = hv_iommu_domain_alloc_paging,
	.probe_device		  = hv_iommu_probe_device,
	.device_group		  = hv_iommu_device_group,
	.get_resv_regions	  = hv_iommu_get_resv_regions,
	.owner			  = THIS_MODULE,
	.identity_domain	  = &hv_identity_domain.domain,
	.blocked_domain		  = &hv_blocking_domain.domain,
	.release_domain		  = &hv_blocking_domain.domain,
};

static u64 hv_iommu_detect(struct hv_output_get_iommu_capabilities *cap)
{
	u64 status;
	unsigned long flags;
	struct hv_input_get_iommu_capabilities *input;
	struct hv_output_get_iommu_capabilities *output;

	local_irq_save(flags);

	/*
	 * The combined input and output fit in the per-CPU input page,
	 * so no separate output page is needed.
	 */
	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	output = (struct hv_output_get_iommu_capabilities *)(input + 1);
	memset(input, 0, sizeof(*input));
	input->partition_id = HV_PARTITION_ID_SELF;
	status = hv_do_hypercall(HVCALL_GET_IOMMU_CAPABILITIES, input, output);
	*cap = *output;

	local_irq_restore(flags);

	return status;
}

static void __init
hv_init_iommu_device(struct hv_iommu_dev *hv_iommu,
		     struct hv_output_get_iommu_capabilities *hv_iommu_cap)
{
	ida_init(&hv_iommu->domain_ids);

	hv_iommu->cap = hv_iommu_cap->iommu_cap;
	hv_iommu->max_iova_width = hv_iommu_cap->max_iova_width;
	if (!hv_iommu_5lvl_supported(hv_iommu->cap) &&
	    hv_iommu->max_iova_width > 48) {
		pr_info("5-level paging not supported, limiting iova width to 48.\n");
		hv_iommu->max_iova_width = 48;
	}

	hv_iommu->first_domain = HV_DEVICE_DOMAIN_ID_DEFAULT + 1;
	hv_iommu->last_domain = HV_DEVICE_DOMAIN_ID_NULL - 1;
	hv_iommu->pgsize_bitmap = hv_iommu_cap->pgsize_bitmap;
	hv_iommu_device = hv_iommu;
}

int __init hv_iommu_init(void)
{
	u64 status;
	int ret = 0;
	struct hv_iommu_dev *hv_iommu = NULL;
	struct hv_output_get_iommu_capabilities hv_iommu_cap = {0};

	if (no_iommu || iommu_detected)
		return -ENODEV;

	if (!hv_is_hyperv_initialized())
		return -ENODEV;

	status = hv_iommu_detect(&hv_iommu_cap);
	if (!hv_result_success(status)) {
		if (hv_result(status) == HV_STATUS_INVALID_HYPERCALL_CODE)
			return -ENODEV;

		hv_status_err(status, "HVCALL_GET_IOMMU_CAPABILITIES failed\n");
		return hv_result_to_errno(status);
	}

	if (!hv_iommu_present(hv_iommu_cap.iommu_cap))
		return -ENODEV;

	if (!hv_iommu_s1_domain_supported(hv_iommu_cap.iommu_cap)) {
		pr_err("stage-1 translation not supported: cap=%#llx\n",
		       hv_iommu_cap.iommu_cap);
		return -ENODEV;
	}

	/*
	 * Require the base page size. The domain page-size bitmap is later
	 * restricted to the sizes supported by both iommupt and Hyper-V.
	 */
	if (!(hv_iommu_cap.pgsize_bitmap & PAGE_SIZE)) {
		pr_err("unsupported page sizes: pgsize_bitmap=0x%llx\n",
		       hv_iommu_cap.pgsize_bitmap);
		return -ENODEV;
	}

	iommu_detected = 1;
	pci_request_acs();

	hv_iommu = kzalloc_obj(*hv_iommu, GFP_KERNEL);
	if (!hv_iommu)
		return -ENOMEM;

	hv_init_iommu_device(hv_iommu, &hv_iommu_cap);

	ret = hv_initialize_static_domains();
	if (ret)
		goto err_free;

	ret = iommu_device_sysfs_add(&hv_iommu->iommu, NULL, NULL, "%s", "hv-iommu");
	if (ret) {
		pr_err("iommu_device_sysfs_add failed: %d\n", ret);
		goto err_delete_static_domains;
	}

	ret = iommu_device_register(&hv_iommu->iommu, &hv_iommu_ops, NULL);
	if (ret) {
		pr_err("iommu_device_register failed: %d\n", ret);
		goto err_sysfs_remove;
	}

	pr_info("initialized: %u-bit IOVA aperture, page-size bitmap %#llx\n",
		hv_iommu->max_iova_width, hv_iommu->pgsize_bitmap);
	return 0;

err_sysfs_remove:
	iommu_device_sysfs_remove(&hv_iommu->iommu);
err_delete_static_domains:
	hv_delete_device_domain(&hv_blocking_domain);
	hv_delete_device_domain(&hv_identity_domain);
err_free:
	kfree(hv_iommu);
	return ret;
}
