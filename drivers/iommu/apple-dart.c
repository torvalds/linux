// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple DART (Device Address Resolution Table) IOMMU driver
 *
 * Copyright (C) 2021 The Asahi Linux Contributors
 *
 * Based on arm/arm-smmu/arm-ssmu.c and arm/arm-smmu-v3/arm-smmu-v3.c
 *  Copyright (C) 2013 ARM Limited
 *  Copyright (C) 2015 ARM Limited
 * and on exynos-iommu.c
 *  Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 */

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dev_printk.h>
#include <linux/dma-iommu.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/swab.h>
#include <linux/types.h>

#define DART_MAX_STREAMS 16
#define DART_MAX_TTBR 4
#define MAX_DARTS_PER_DEVICE 2

#define DART_STREAM_ALL 0xffff

#define DART_PARAMS1 0x00
#define DART_PARAMS_PAGE_SHIFT GENMASK(27, 24)

#define DART_PARAMS2 0x04
#define DART_PARAMS_BYPASS_SUPPORT BIT(0)

#define DART_STREAM_COMMAND 0x20
#define DART_STREAM_COMMAND_BUSY BIT(2)
#define DART_STREAM_COMMAND_INVALIDATE BIT(20)

#define DART_STREAM_SELECT 0x34

#define DART_ERROR 0x40
#define DART_ERROR_STREAM GENMASK(27, 24)
#define DART_ERROR_CODE GENMASK(11, 0)
#define DART_ERROR_FLAG BIT(31)

#define DART_ERROR_READ_FAULT BIT(4)
#define DART_ERROR_WRITE_FAULT BIT(3)
#define DART_ERROR_NO_PTE BIT(2)
#define DART_ERROR_NO_PMD BIT(1)
#define DART_ERROR_NO_TTBR BIT(0)

#define DART_CONFIG 0x60
#define DART_CONFIG_LOCK BIT(15)

#define DART_STREAM_COMMAND_BUSY_TIMEOUT 100

#define DART_ERROR_ADDR_HI 0x54
#define DART_ERROR_ADDR_LO 0x50

#define DART_STREAMS_ENABLE 0xfc

#define DART_TCR(sid) (0x100 + 4 * (sid))
#define DART_TCR_TRANSLATE_ENABLE BIT(7)
#define DART_TCR_BYPASS0_ENABLE BIT(8)
#define DART_TCR_BYPASS1_ENABLE BIT(12)

#define DART_TTBR(sid, idx) (0x200 + 16 * (sid) + 4 * (idx))
#define DART_TTBR_VALID BIT(31)
#define DART_TTBR_SHIFT 12

/*
 * Private structure associated with each DART device.
 *
 * @dev: device struct
 * @regs: mapped MMIO region
 * @irq: interrupt number, can be shared with other DARTs
 * @clks: clocks associated with this DART
 * @num_clks: number of @clks
 * @lock: lock for hardware operations involving this dart
 * @pgsize: pagesize supported by this DART
 * @supports_bypass: indicates if this DART supports bypass mode
 * @force_bypass: force bypass mode due to pagesize mismatch?
 * @sid2group: maps stream ids to iommu_groups
 * @iommu: iommu core device
 */
struct apple_dart {
	struct device *dev;

	void __iomem *regs;

	int irq;
	struct clk_bulk_data *clks;
	int num_clks;

	spinlock_t lock;

	u32 pgsize;
	u32 supports_bypass : 1;
	u32 force_bypass : 1;

	struct iommu_group *sid2group[DART_MAX_STREAMS];
	struct iommu_device iommu;
};

/*
 * Convenience struct to identify streams.
 *
 * The normal variant is used inside apple_dart_master_cfg which isn't written
 * to concurrently.
 * The atomic variant is used inside apple_dart_domain where we have to guard
 * against races from potential parallel calls to attach/detach_device.
 * Note that even inside the atomic variant the apple_dart pointer is not
 * protected: This pointer is initialized once under the domain init mutex
 * and never changed again afterwards. Devices with different dart pointers
 * cannot be attached to the same domain.
 *
 * @dart dart pointer
 * @sid stream id bitmap
 */
struct apple_dart_stream_map {
	struct apple_dart *dart;
	unsigned long sidmap;
};
struct apple_dart_atomic_stream_map {
	struct apple_dart *dart;
	atomic64_t sidmap;
};

/*
 * This structure is attached to each iommu domain handled by a DART.
 *
 * @pgtbl_ops: pagetable ops allocated by io-pgtable
 * @finalized: true if the domain has been completely initialized
 * @init_lock: protects domain initialization
 * @stream_maps: streams attached to this domain (valid for DMA/UNMANAGED only)
 * @domain: core iommu domain pointer
 */
struct apple_dart_domain {
	struct io_pgtable_ops *pgtbl_ops;

	bool finalized;
	struct mutex init_lock;
	struct apple_dart_atomic_stream_map stream_maps[MAX_DARTS_PER_DEVICE];

	struct iommu_domain domain;
};

/*
 * This structure is attached to devices with dev_iommu_priv_set() on of_xlate
 * and contains a list of streams bound to this device.
 * So far the worst case seen is a single device with two streams
 * from different darts, such that this simple static array is enough.
 *
 * @streams: streams for this device
 */
struct apple_dart_master_cfg {
	struct apple_dart_stream_map stream_maps[MAX_DARTS_PER_DEVICE];
};

/*
 * Helper macro to iterate over apple_dart_master_cfg.stream_maps and
 * apple_dart_domain.stream_maps
 *
 * @i int used as loop variable
 * @base pointer to base struct (apple_dart_master_cfg or apple_dart_domain)
 * @stream pointer to the apple_dart_streams struct for each loop iteration
 */
#define for_each_stream_map(i, base, stream_map)                               \
	for (i = 0, stream_map = &(base)->stream_maps[0];                      \
	     i < MAX_DARTS_PER_DEVICE && stream_map->dart;                     \
	     stream_map = &(base)->stream_maps[++i])

static struct platform_driver apple_dart_driver;
static const struct iommu_ops apple_dart_iommu_ops;

static struct apple_dart_domain *to_dart_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct apple_dart_domain, domain);
}

static void
apple_dart_hw_enable_translation(struct apple_dart_stream_map *stream_map)
{
	int sid;

	for_each_set_bit(sid, &stream_map->sidmap, DART_MAX_STREAMS)
		writel(DART_TCR_TRANSLATE_ENABLE,
		       stream_map->dart->regs + DART_TCR(sid));
}

static void apple_dart_hw_disable_dma(struct apple_dart_stream_map *stream_map)
{
	int sid;

	for_each_set_bit(sid, &stream_map->sidmap, DART_MAX_STREAMS)
		writel(0, stream_map->dart->regs + DART_TCR(sid));
}

static void
apple_dart_hw_enable_bypass(struct apple_dart_stream_map *stream_map)
{
	int sid;

	WARN_ON(!stream_map->dart->supports_bypass);
	for_each_set_bit(sid, &stream_map->sidmap, DART_MAX_STREAMS)
		writel(DART_TCR_BYPASS0_ENABLE | DART_TCR_BYPASS1_ENABLE,
		       stream_map->dart->regs + DART_TCR(sid));
}

static void apple_dart_hw_set_ttbr(struct apple_dart_stream_map *stream_map,
				   u8 idx, phys_addr_t paddr)
{
	int sid;

	WARN_ON(paddr & ((1 << DART_TTBR_SHIFT) - 1));
	for_each_set_bit(sid, &stream_map->sidmap, DART_MAX_STREAMS)
		writel(DART_TTBR_VALID | (paddr >> DART_TTBR_SHIFT),
		       stream_map->dart->regs + DART_TTBR(sid, idx));
}

static void apple_dart_hw_clear_ttbr(struct apple_dart_stream_map *stream_map,
				     u8 idx)
{
	int sid;

	for_each_set_bit(sid, &stream_map->sidmap, DART_MAX_STREAMS)
		writel(0, stream_map->dart->regs + DART_TTBR(sid, idx));
}

static void
apple_dart_hw_clear_all_ttbrs(struct apple_dart_stream_map *stream_map)
{
	int i;

	for (i = 0; i < DART_MAX_TTBR; ++i)
		apple_dart_hw_clear_ttbr(stream_map, i);
}

static int
apple_dart_hw_stream_command(struct apple_dart_stream_map *stream_map,
			     u32 command)
{
	unsigned long flags;
	int ret;
	u32 command_reg;

	spin_lock_irqsave(&stream_map->dart->lock, flags);

	writel(stream_map->sidmap, stream_map->dart->regs + DART_STREAM_SELECT);
	writel(command, stream_map->dart->regs + DART_STREAM_COMMAND);

	ret = readl_poll_timeout_atomic(
		stream_map->dart->regs + DART_STREAM_COMMAND, command_reg,
		!(command_reg & DART_STREAM_COMMAND_BUSY), 1,
		DART_STREAM_COMMAND_BUSY_TIMEOUT);

	spin_unlock_irqrestore(&stream_map->dart->lock, flags);

	if (ret) {
		dev_err(stream_map->dart->dev,
			"busy bit did not clear after command %x for streams %lx\n",
			command, stream_map->sidmap);
		return ret;
	}

	return 0;
}

static int
apple_dart_hw_invalidate_tlb(struct apple_dart_stream_map *stream_map)
{
	return apple_dart_hw_stream_command(stream_map,
					    DART_STREAM_COMMAND_INVALIDATE);
}

static int apple_dart_hw_reset(struct apple_dart *dart)
{
	u32 config;
	struct apple_dart_stream_map stream_map;

	config = readl(dart->regs + DART_CONFIG);
	if (config & DART_CONFIG_LOCK) {
		dev_err(dart->dev, "DART is locked down until reboot: %08x\n",
			config);
		return -EINVAL;
	}

	stream_map.dart = dart;
	stream_map.sidmap = DART_STREAM_ALL;
	apple_dart_hw_disable_dma(&stream_map);
	apple_dart_hw_clear_all_ttbrs(&stream_map);

	/* enable all streams globally since TCR is used to control isolation */
	writel(DART_STREAM_ALL, dart->regs + DART_STREAMS_ENABLE);

	/* clear any pending errors before the interrupt is unmasked */
	writel(readl(dart->regs + DART_ERROR), dart->regs + DART_ERROR);

	return apple_dart_hw_invalidate_tlb(&stream_map);
}

static void apple_dart_domain_flush_tlb(struct apple_dart_domain *domain)
{
	int i;
	struct apple_dart_atomic_stream_map *domain_stream_map;
	struct apple_dart_stream_map stream_map;

	for_each_stream_map(i, domain, domain_stream_map) {
		stream_map.dart = domain_stream_map->dart;
		stream_map.sidmap = atomic64_read(&domain_stream_map->sidmap);
		apple_dart_hw_invalidate_tlb(&stream_map);
	}
}

static void apple_dart_flush_iotlb_all(struct iommu_domain *domain)
{
	apple_dart_domain_flush_tlb(to_dart_domain(domain));
}

static void apple_dart_iotlb_sync(struct iommu_domain *domain,
				  struct iommu_iotlb_gather *gather)
{
	apple_dart_domain_flush_tlb(to_dart_domain(domain));
}

static void apple_dart_iotlb_sync_map(struct iommu_domain *domain,
				      unsigned long iova, size_t size)
{
	apple_dart_domain_flush_tlb(to_dart_domain(domain));
}

static phys_addr_t apple_dart_iova_to_phys(struct iommu_domain *domain,
					   dma_addr_t iova)
{
	struct apple_dart_domain *dart_domain = to_dart_domain(domain);
	struct io_pgtable_ops *ops = dart_domain->pgtbl_ops;

	if (!ops)
		return 0;

	return ops->iova_to_phys(ops, iova);
}

static int apple_dart_map_pages(struct iommu_domain *domain, unsigned long iova,
				phys_addr_t paddr, size_t pgsize,
				size_t pgcount, int prot, gfp_t gfp,
				size_t *mapped)
{
	struct apple_dart_domain *dart_domain = to_dart_domain(domain);
	struct io_pgtable_ops *ops = dart_domain->pgtbl_ops;

	if (!ops)
		return -ENODEV;

	return ops->map_pages(ops, iova, paddr, pgsize, pgcount, prot, gfp,
			      mapped);
}

static size_t apple_dart_unmap_pages(struct iommu_domain *domain,
				     unsigned long iova, size_t pgsize,
				     size_t pgcount,
				     struct iommu_iotlb_gather *gather)
{
	struct apple_dart_domain *dart_domain = to_dart_domain(domain);
	struct io_pgtable_ops *ops = dart_domain->pgtbl_ops;

	return ops->unmap_pages(ops, iova, pgsize, pgcount, gather);
}

static void
apple_dart_setup_translation(struct apple_dart_domain *domain,
			     struct apple_dart_stream_map *stream_map)
{
	int i;
	struct io_pgtable_cfg *pgtbl_cfg =
		&io_pgtable_ops_to_pgtable(domain->pgtbl_ops)->cfg;

	for (i = 0; i < pgtbl_cfg->apple_dart_cfg.n_ttbrs; ++i)
		apple_dart_hw_set_ttbr(stream_map, i,
				       pgtbl_cfg->apple_dart_cfg.ttbr[i]);
	for (; i < DART_MAX_TTBR; ++i)
		apple_dart_hw_clear_ttbr(stream_map, i);

	apple_dart_hw_enable_translation(stream_map);
	apple_dart_hw_invalidate_tlb(stream_map);
}

static int apple_dart_finalize_domain(struct iommu_domain *domain,
				      struct apple_dart_master_cfg *cfg)
{
	struct apple_dart_domain *dart_domain = to_dart_domain(domain);
	struct apple_dart *dart = cfg->stream_maps[0].dart;
	struct io_pgtable_cfg pgtbl_cfg;
	int ret = 0;
	int i;

	mutex_lock(&dart_domain->init_lock);

	if (dart_domain->finalized)
		goto done;

	for (i = 0; i < MAX_DARTS_PER_DEVICE; ++i) {
		dart_domain->stream_maps[i].dart = cfg->stream_maps[i].dart;
		atomic64_set(&dart_domain->stream_maps[i].sidmap,
			     cfg->stream_maps[i].sidmap);
	}

	pgtbl_cfg = (struct io_pgtable_cfg){
		.pgsize_bitmap = dart->pgsize,
		.ias = 32,
		.oas = 36,
		.coherent_walk = 1,
		.iommu_dev = dart->dev,
	};

	dart_domain->pgtbl_ops =
		alloc_io_pgtable_ops(APPLE_DART, &pgtbl_cfg, domain);
	if (!dart_domain->pgtbl_ops) {
		ret = -ENOMEM;
		goto done;
	}

	domain->pgsize_bitmap = pgtbl_cfg.pgsize_bitmap;
	domain->geometry.aperture_start = 0;
	domain->geometry.aperture_end = DMA_BIT_MASK(32);
	domain->geometry.force_aperture = true;

	dart_domain->finalized = true;

done:
	mutex_unlock(&dart_domain->init_lock);
	return ret;
}

static int
apple_dart_mod_streams(struct apple_dart_atomic_stream_map *domain_maps,
		       struct apple_dart_stream_map *master_maps,
		       bool add_streams)
{
	int i;

	for (i = 0; i < MAX_DARTS_PER_DEVICE; ++i) {
		if (domain_maps[i].dart != master_maps[i].dart)
			return -EINVAL;
	}

	for (i = 0; i < MAX_DARTS_PER_DEVICE; ++i) {
		if (!domain_maps[i].dart)
			break;
		if (add_streams)
			atomic64_or(master_maps[i].sidmap,
				    &domain_maps[i].sidmap);
		else
			atomic64_and(~master_maps[i].sidmap,
				     &domain_maps[i].sidmap);
	}

	return 0;
}

static int apple_dart_domain_add_streams(struct apple_dart_domain *domain,
					 struct apple_dart_master_cfg *cfg)
{
	return apple_dart_mod_streams(domain->stream_maps, cfg->stream_maps,
				      true);
}

static int apple_dart_domain_remove_streams(struct apple_dart_domain *domain,
					    struct apple_dart_master_cfg *cfg)
{
	return apple_dart_mod_streams(domain->stream_maps, cfg->stream_maps,
				      false);
}

static int apple_dart_attach_dev(struct iommu_domain *domain,
				 struct device *dev)
{
	int ret, i;
	struct apple_dart_stream_map *stream_map;
	struct apple_dart_master_cfg *cfg = dev_iommu_priv_get(dev);
	struct apple_dart_domain *dart_domain = to_dart_domain(domain);

	if (cfg->stream_maps[0].dart->force_bypass &&
	    domain->type != IOMMU_DOMAIN_IDENTITY)
		return -EINVAL;
	if (!cfg->stream_maps[0].dart->supports_bypass &&
	    domain->type == IOMMU_DOMAIN_IDENTITY)
		return -EINVAL;

	ret = apple_dart_finalize_domain(domain, cfg);
	if (ret)
		return ret;

	switch (domain->type) {
	case IOMMU_DOMAIN_DMA:
	case IOMMU_DOMAIN_UNMANAGED:
		ret = apple_dart_domain_add_streams(dart_domain, cfg);
		if (ret)
			return ret;

		for_each_stream_map(i, cfg, stream_map)
			apple_dart_setup_translation(dart_domain, stream_map);
		break;
	case IOMMU_DOMAIN_BLOCKED:
		for_each_stream_map(i, cfg, stream_map)
			apple_dart_hw_disable_dma(stream_map);
		break;
	case IOMMU_DOMAIN_IDENTITY:
		for_each_stream_map(i, cfg, stream_map)
			apple_dart_hw_enable_bypass(stream_map);
		break;
	}

	return ret;
}

static void apple_dart_detach_dev(struct iommu_domain *domain,
				  struct device *dev)
{
	int i;
	struct apple_dart_stream_map *stream_map;
	struct apple_dart_master_cfg *cfg = dev_iommu_priv_get(dev);
	struct apple_dart_domain *dart_domain = to_dart_domain(domain);

	for_each_stream_map(i, cfg, stream_map)
		apple_dart_hw_disable_dma(stream_map);

	if (domain->type == IOMMU_DOMAIN_DMA ||
	    domain->type == IOMMU_DOMAIN_UNMANAGED)
		apple_dart_domain_remove_streams(dart_domain, cfg);
}

static struct iommu_device *apple_dart_probe_device(struct device *dev)
{
	struct apple_dart_master_cfg *cfg = dev_iommu_priv_get(dev);
	struct apple_dart_stream_map *stream_map;
	int i;

	if (!cfg)
		return ERR_PTR(-ENODEV);

	for_each_stream_map(i, cfg, stream_map)
		device_link_add(
			dev, stream_map->dart->dev,
			DL_FLAG_PM_RUNTIME | DL_FLAG_AUTOREMOVE_SUPPLIER);

	return &cfg->stream_maps[0].dart->iommu;
}

static void apple_dart_release_device(struct device *dev)
{
	struct apple_dart_master_cfg *cfg = dev_iommu_priv_get(dev);

	if (!cfg)
		return;

	dev_iommu_priv_set(dev, NULL);
	kfree(cfg);
}

static struct iommu_domain *apple_dart_domain_alloc(unsigned int type)
{
	struct apple_dart_domain *dart_domain;

	if (type != IOMMU_DOMAIN_DMA && type != IOMMU_DOMAIN_UNMANAGED &&
	    type != IOMMU_DOMAIN_IDENTITY && type != IOMMU_DOMAIN_BLOCKED)
		return NULL;

	dart_domain = kzalloc(sizeof(*dart_domain), GFP_KERNEL);
	if (!dart_domain)
		return NULL;

	iommu_get_dma_cookie(&dart_domain->domain);
	mutex_init(&dart_domain->init_lock);

	/* no need to allocate pgtbl_ops or do any other finalization steps */
	if (type == IOMMU_DOMAIN_IDENTITY || type == IOMMU_DOMAIN_BLOCKED)
		dart_domain->finalized = true;

	return &dart_domain->domain;
}

static void apple_dart_domain_free(struct iommu_domain *domain)
{
	struct apple_dart_domain *dart_domain = to_dart_domain(domain);

	if (dart_domain->pgtbl_ops)
		free_io_pgtable_ops(dart_domain->pgtbl_ops);

	kfree(dart_domain);
}

static int apple_dart_of_xlate(struct device *dev, struct of_phandle_args *args)
{
	struct apple_dart_master_cfg *cfg = dev_iommu_priv_get(dev);
	struct platform_device *iommu_pdev = of_find_device_by_node(args->np);
	struct apple_dart *dart = platform_get_drvdata(iommu_pdev);
	struct apple_dart *cfg_dart;
	int i, sid;

	if (args->args_count != 1)
		return -EINVAL;
	sid = args->args[0];

	if (!cfg)
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;
	dev_iommu_priv_set(dev, cfg);

	cfg_dart = cfg->stream_maps[0].dart;
	if (cfg_dart) {
		if (cfg_dart->supports_bypass != dart->supports_bypass)
			return -EINVAL;
		if (cfg_dart->force_bypass != dart->force_bypass)
			return -EINVAL;
		if (cfg_dart->pgsize != dart->pgsize)
			return -EINVAL;
	}

	for (i = 0; i < MAX_DARTS_PER_DEVICE; ++i) {
		if (cfg->stream_maps[i].dart == dart) {
			cfg->stream_maps[i].sidmap |= 1 << sid;
			return 0;
		}
	}
	for (i = 0; i < MAX_DARTS_PER_DEVICE; ++i) {
		if (!cfg->stream_maps[i].dart) {
			cfg->stream_maps[i].dart = dart;
			cfg->stream_maps[i].sidmap = 1 << sid;
			return 0;
		}
	}

	return -EINVAL;
}

static DEFINE_MUTEX(apple_dart_groups_lock);

static void apple_dart_release_group(void *iommu_data)
{
	int i, sid;
	struct apple_dart_stream_map *stream_map;
	struct apple_dart_master_cfg *group_master_cfg = iommu_data;

	mutex_lock(&apple_dart_groups_lock);

	for_each_stream_map(i, group_master_cfg, stream_map)
		for_each_set_bit(sid, &stream_map->sidmap, DART_MAX_STREAMS)
			stream_map->dart->sid2group[sid] = NULL;

	kfree(iommu_data);
	mutex_unlock(&apple_dart_groups_lock);
}

static struct iommu_group *apple_dart_device_group(struct device *dev)
{
	int i, sid;
	struct apple_dart_master_cfg *cfg = dev_iommu_priv_get(dev);
	struct apple_dart_stream_map *stream_map;
	struct apple_dart_master_cfg *group_master_cfg;
	struct iommu_group *group = NULL;
	struct iommu_group *res = ERR_PTR(-EINVAL);

	mutex_lock(&apple_dart_groups_lock);

	for_each_stream_map(i, cfg, stream_map) {
		for_each_set_bit(sid, &stream_map->sidmap, DART_MAX_STREAMS) {
			struct iommu_group *stream_group =
				stream_map->dart->sid2group[sid];

			if (group && group != stream_group) {
				res = ERR_PTR(-EINVAL);
				goto out;
			}

			group = stream_group;
		}
	}

	if (group) {
		res = iommu_group_ref_get(group);
		goto out;
	}

#ifdef CONFIG_PCI
	if (dev_is_pci(dev))
		group = pci_device_group(dev);
	else
#endif
		group = generic_device_group(dev);

	res = ERR_PTR(-ENOMEM);
	if (!group)
		goto out;

	group_master_cfg = kzalloc(sizeof(*group_master_cfg), GFP_KERNEL);
	if (!group_master_cfg) {
		iommu_group_put(group);
		goto out;
	}

	memcpy(group_master_cfg, cfg, sizeof(*group_master_cfg));
	iommu_group_set_iommudata(group, group_master_cfg,
		apple_dart_release_group);

	for_each_stream_map(i, cfg, stream_map)
		for_each_set_bit(sid, &stream_map->sidmap, DART_MAX_STREAMS)
			stream_map->dart->sid2group[sid] = group;

	res = group;

out:
	mutex_unlock(&apple_dart_groups_lock);
	return res;
}

static int apple_dart_def_domain_type(struct device *dev)
{
	struct apple_dart_master_cfg *cfg = dev_iommu_priv_get(dev);

	if (cfg->stream_maps[0].dart->force_bypass)
		return IOMMU_DOMAIN_IDENTITY;
	if (!cfg->stream_maps[0].dart->supports_bypass)
		return IOMMU_DOMAIN_DMA;

	return 0;
}

static const struct iommu_ops apple_dart_iommu_ops = {
	.domain_alloc = apple_dart_domain_alloc,
	.domain_free = apple_dart_domain_free,
	.attach_dev = apple_dart_attach_dev,
	.detach_dev = apple_dart_detach_dev,
	.map_pages = apple_dart_map_pages,
	.unmap_pages = apple_dart_unmap_pages,
	.flush_iotlb_all = apple_dart_flush_iotlb_all,
	.iotlb_sync = apple_dart_iotlb_sync,
	.iotlb_sync_map = apple_dart_iotlb_sync_map,
	.iova_to_phys = apple_dart_iova_to_phys,
	.probe_device = apple_dart_probe_device,
	.release_device = apple_dart_release_device,
	.device_group = apple_dart_device_group,
	.of_xlate = apple_dart_of_xlate,
	.def_domain_type = apple_dart_def_domain_type,
	.pgsize_bitmap = -1UL, /* Restricted during dart probe */
	.owner = THIS_MODULE,
};

static irqreturn_t apple_dart_irq(int irq, void *dev)
{
	struct apple_dart *dart = dev;
	const char *fault_name = NULL;
	u32 error = readl(dart->regs + DART_ERROR);
	u32 error_code = FIELD_GET(DART_ERROR_CODE, error);
	u32 addr_lo = readl(dart->regs + DART_ERROR_ADDR_LO);
	u32 addr_hi = readl(dart->regs + DART_ERROR_ADDR_HI);
	u64 addr = addr_lo | (((u64)addr_hi) << 32);
	u8 stream_idx = FIELD_GET(DART_ERROR_STREAM, error);

	if (!(error & DART_ERROR_FLAG))
		return IRQ_NONE;

	/* there should only be a single bit set but let's use == to be sure */
	if (error_code == DART_ERROR_READ_FAULT)
		fault_name = "READ FAULT";
	else if (error_code == DART_ERROR_WRITE_FAULT)
		fault_name = "WRITE FAULT";
	else if (error_code == DART_ERROR_NO_PTE)
		fault_name = "NO PTE FOR IOVA";
	else if (error_code == DART_ERROR_NO_PMD)
		fault_name = "NO PMD FOR IOVA";
	else if (error_code == DART_ERROR_NO_TTBR)
		fault_name = "NO TTBR FOR IOVA";
	else
		fault_name = "unknown";

	dev_err_ratelimited(
		dart->dev,
		"translation fault: status:0x%x stream:%d code:0x%x (%s) at 0x%llx",
		error, stream_idx, error_code, fault_name, addr);

	writel(error, dart->regs + DART_ERROR);
	return IRQ_HANDLED;
}

static int apple_dart_set_bus_ops(const struct iommu_ops *ops)
{
	int ret;

	if (!iommu_present(&platform_bus_type)) {
		ret = bus_set_iommu(&platform_bus_type, ops);
		if (ret)
			return ret;
	}
#ifdef CONFIG_PCI
	if (!iommu_present(&pci_bus_type)) {
		ret = bus_set_iommu(&pci_bus_type, ops);
		if (ret) {
			bus_set_iommu(&platform_bus_type, NULL);
			return ret;
		}
	}
#endif
	return 0;
}

static int apple_dart_probe(struct platform_device *pdev)
{
	int ret;
	u32 dart_params[2];
	struct resource *res;
	struct apple_dart *dart;
	struct device *dev = &pdev->dev;

	dart = devm_kzalloc(dev, sizeof(*dart), GFP_KERNEL);
	if (!dart)
		return -ENOMEM;

	dart->dev = dev;
	spin_lock_init(&dart->lock);

	dart->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(dart->regs))
		return PTR_ERR(dart->regs);

	if (resource_size(res) < 0x4000) {
		dev_err(dev, "MMIO region too small (%pr)\n", res);
		return -EINVAL;
	}

	dart->irq = platform_get_irq(pdev, 0);
	if (dart->irq < 0)
		return -ENODEV;

	ret = devm_clk_bulk_get_all(dev, &dart->clks);
	if (ret < 0)
		return ret;
	dart->num_clks = ret;

	ret = clk_bulk_prepare_enable(dart->num_clks, dart->clks);
	if (ret)
		return ret;

	ret = apple_dart_hw_reset(dart);
	if (ret)
		goto err_clk_disable;

	dart_params[0] = readl(dart->regs + DART_PARAMS1);
	dart_params[1] = readl(dart->regs + DART_PARAMS2);
	dart->pgsize = 1 << FIELD_GET(DART_PARAMS_PAGE_SHIFT, dart_params[0]);
	dart->supports_bypass = dart_params[1] & DART_PARAMS_BYPASS_SUPPORT;
	dart->force_bypass = dart->pgsize > PAGE_SIZE;

	ret = request_irq(dart->irq, apple_dart_irq, IRQF_SHARED,
			  "apple-dart fault handler", dart);
	if (ret)
		goto err_clk_disable;

	platform_set_drvdata(pdev, dart);

	ret = apple_dart_set_bus_ops(&apple_dart_iommu_ops);
	if (ret)
		goto err_free_irq;

	ret = iommu_device_sysfs_add(&dart->iommu, dev, NULL, "apple-dart.%s",
				     dev_name(&pdev->dev));
	if (ret)
		goto err_remove_bus_ops;

	ret = iommu_device_register(&dart->iommu, &apple_dart_iommu_ops, dev);
	if (ret)
		goto err_sysfs_remove;

	dev_info(
		&pdev->dev,
		"DART [pagesize %x, bypass support: %d, bypass forced: %d] initialized\n",
		dart->pgsize, dart->supports_bypass, dart->force_bypass);
	return 0;

err_sysfs_remove:
	iommu_device_sysfs_remove(&dart->iommu);
err_remove_bus_ops:
	apple_dart_set_bus_ops(NULL);
err_free_irq:
	free_irq(dart->irq, dart);
err_clk_disable:
	clk_bulk_disable_unprepare(dart->num_clks, dart->clks);

	return ret;
}

static int apple_dart_remove(struct platform_device *pdev)
{
	struct apple_dart *dart = platform_get_drvdata(pdev);

	apple_dart_hw_reset(dart);
	free_irq(dart->irq, dart);
	apple_dart_set_bus_ops(NULL);

	iommu_device_unregister(&dart->iommu);
	iommu_device_sysfs_remove(&dart->iommu);

	clk_bulk_disable_unprepare(dart->num_clks, dart->clks);

	return 0;
}

static const struct of_device_id apple_dart_of_match[] = {
	{ .compatible = "apple,t8103-dart", .data = NULL },
	{},
};
MODULE_DEVICE_TABLE(of, apple_dart_of_match);

static struct platform_driver apple_dart_driver = {
	.driver	= {
		.name			= "apple-dart",
		.of_match_table		= apple_dart_of_match,
		.suppress_bind_attrs    = true,
	},
	.probe	= apple_dart_probe,
	.remove	= apple_dart_remove,
};

module_platform_driver(apple_dart_driver);

MODULE_DESCRIPTION("IOMMU API for Apple's DART");
MODULE_AUTHOR("Sven Peter <sven@svenpeter.dev>");
MODULE_LICENSE("GPL v2");
