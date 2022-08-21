// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/iommu.h>
#include <linux/qcom-io-pgtable.h>
#include <linux/slab.h>

#include "iommu-logger.h"

static DEFINE_MUTEX(iommu_debug_attachments_lock);
static LIST_HEAD(iommu_debug_attachments);

static unsigned int iommu_logger_pgtable_levels(struct io_pgtable *iop)
{
	unsigned int va_bits, pte_size, bits_per_level, pg_shift;
	unsigned long ias = iop->cfg.ias;

	switch ((u32)iop->fmt) {
	case ARM_32_LPAE_S1:
	case ARM_64_LPAE_S1:
#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST
	case ARM_V8L_FAST:
#endif
	case QCOM_ARM_64_LPAE_S1:
		pte_size = sizeof(u64);
		break;
	default:
		return 0;
	}

	pg_shift = __ffs(iop->cfg.pgsize_bitmap);
	bits_per_level = pg_shift - ilog2(pte_size);
	va_bits = ias - pg_shift;
	return DIV_ROUND_UP(va_bits, bits_per_level);
}

static enum iommu_logger_pgtable_fmt iommu_logger_pgtable_fmt_lut(
							enum io_pgtable_fmt fmt)
{
	switch ((u32)fmt) {
	case ARM_32_LPAE_S1:
		return IOMMU_LOGGER_ARM_32_LPAE_S1;
	case ARM_64_LPAE_S1:
#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST
	case ARM_V8L_FAST:
#endif
	case QCOM_ARM_64_LPAE_S1:
		return IOMMU_LOGGER_ARM_64_LPAE_S1;
	default:
		return IOMMU_LOGGER_MAX_PGTABLE_FMTS;
	}
}

static int iommu_logger_domain_ttbrs(struct io_pgtable *iop, void **ttbr0_ptr,
				     void **ttbr1_ptr)
{
	int ret;
	u64 ttbr0;

	switch ((u32)iop->fmt) {
	case ARM_32_LPAE_S1:
	case ARM_64_LPAE_S1:
#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST
	case ARM_V8L_FAST:
#endif
	case QCOM_ARM_64_LPAE_S1:
		ttbr0 = iop->cfg.arm_lpae_s1_cfg.ttbr;
		ret = 0;
		break;
	default:
		ret = -EINVAL;
	}

	if (!ret) {
		*ttbr0_ptr = phys_to_virt(ttbr0);
		/*
		 * FIXME - fix ttbr1 retrieval later. In this kernel version
		 * struct io_pgtable no longer contains this information.
		 */
		*ttbr1_ptr = NULL;
	}

	return ret;
}

static struct iommu_debug_attachment *iommu_logger_init(
						struct iommu_domain *domain,
						struct device *dev,
						struct io_pgtable *iop)
{
	struct iommu_debug_attachment *logger;
	char *client_name;
	struct iommu_group *group;
	unsigned int levels = iommu_logger_pgtable_levels(iop);
	enum iommu_logger_pgtable_fmt fmt = iommu_logger_pgtable_fmt_lut(
								iop->fmt);
	void *ttbr0, *ttbr1;
	int ret;

	if (!levels || fmt == IOMMU_LOGGER_MAX_PGTABLE_FMTS)
		return ERR_PTR(-EINVAL);

	ret = iommu_logger_domain_ttbrs(iop, &ttbr0, &ttbr1);
	if (ret)
		return ERR_PTR(ret);

	logger = kzalloc(sizeof(*logger), GFP_KERNEL);
	if (!logger)
		return ERR_PTR(-ENOMEM);

	client_name = kasprintf(GFP_KERNEL, "%s", kobject_name(&dev->kobj));
	if (!client_name) {
		kfree(logger);
		return ERR_PTR(-ENOMEM);
	}

	group = iommu_group_get(dev);
	iommu_group_put(group);

	INIT_LIST_HEAD(&logger->list);
	logger->domain = domain;
	logger->group = group;
	logger->client_name = client_name;
	logger->fmt = fmt;
	logger->levels = levels;
	logger->ttbr0 = ttbr0;
	logger->ttbr1 = ttbr1;

	return logger;
}

int iommu_logger_register(struct iommu_debug_attachment **logger_out,
			  struct iommu_domain *domain, struct device *dev,
			  struct io_pgtable *iop)
{
	struct iommu_debug_attachment *logger;

	if (!logger_out || !dev || !iop)
		return -EINVAL;

	logger = iommu_logger_init(domain, dev, iop);
	if (IS_ERR(logger))
		return PTR_ERR(logger);

	mutex_lock(&iommu_debug_attachments_lock);
	list_add(&logger->list, &iommu_debug_attachments);
	mutex_unlock(&iommu_debug_attachments_lock);

	*logger_out = logger;
	return 0;
}
EXPORT_SYMBOL(iommu_logger_register);

void iommu_logger_unregister(struct iommu_debug_attachment *logger)
{
	if (!logger)
		return;

	mutex_lock(&iommu_debug_attachments_lock);
	list_del(&logger->list);
	mutex_unlock(&iommu_debug_attachments_lock);
	kfree(logger->client_name);
	kfree(logger);
}
EXPORT_SYMBOL(iommu_logger_unregister);

MODULE_DESCRIPTION("QTI IOMMU SUPPORT");
MODULE_LICENSE("GPL v2");
