// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/kvm_host.h>
#include <linux/of_platform.h>

#include <asm/kvm_mmu.h>
#include <asm/kvm_s2mpu.h>

#define S2MPU_MMIO_SIZE		SZ_64K

#define CTX_CFG_ENTRY(ctxid, nr_ctx, vid) \
	(CONTEXT_CFG_VALID_VID_CTX_VID(ctxid, vid) \
	 | (((ctxid) < (nr_ctx)) ? CONTEXT_CFG_VALID_VID_CTX_VALID(ctxid) : 0))

struct s2mpu_irq_info {
	struct device *dev;
	void __iomem *va;
};

static irqreturn_t s2mpu_irq_handler(int irq, void *data)
{
	struct s2mpu_irq_info *info = data;
	unsigned int vid;
	u32 vid_bmap, fault_info;
	phys_addr_t fault_pa;
	const char *fault_type;
	irqreturn_t ret = IRQ_NONE;

	while ((vid_bmap = readl_relaxed(info->va + REG_NS_FAULT_STATUS))) {
		WARN_ON_ONCE(vid_bmap & (~ALL_VIDS_BITMAP));
		vid = __ffs(vid_bmap);

		fault_pa = hi_lo_readq_relaxed(info->va + REG_NS_FAULT_PA_HIGH_LOW(vid));
		fault_info = readl_relaxed(info->va + REG_NS_FAULT_INFO(vid));
		WARN_ON(FIELD_GET(FAULT_INFO_VID_MASK, fault_info) != vid);

		switch (FIELD_GET(FAULT_INFO_TYPE_MASK, fault_info)) {
		case FAULT_INFO_TYPE_MPTW:
			fault_type = "MPTW fault";
			break;
		case FAULT_INFO_TYPE_AP:
			fault_type = "access permission fault";
			break;
		case FAULT_INFO_TYPE_CONTEXT:
			fault_type = "context fault";
			break;
		default:
			fault_type = "unknown fault";
			break;
		}

		dev_err(info->dev, "\n"
			"============== S2MPU FAULT DETECTED ==============\n"
			"  PA=0x%pap, FAULT_INFO=0x%08x\n"
			"  DIRECTION: %s, TYPE: %s\n"
			"  VID=%u, REQ_LENGTH=%lu, REQ_AXI_ID=%lu\n"
			"==================================================\n",
			&fault_pa, fault_info,
			(fault_info & FAULT_INFO_RW_BIT) ? "write" : "read",
			fault_type, vid,
			FIELD_GET(FAULT_INFO_LEN_MASK, fault_info),
			FIELD_GET(FAULT_INFO_ID_MASK, fault_info));

		writel_relaxed(BIT(vid), info->va + REG_NS_INTERRUPT_CLEAR);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static u32 gen_ctx_cfg_valid_vid(struct platform_device *pdev,
				 unsigned int num_ctx, u32 vid_bmap)
{
	u8 ctx_vid[NR_CTX_IDS] = { 0 };
	unsigned int vid, ctx = 0;

	/* Check NUM_CONTEXT value is within bounds. This should not happen. */
	if (WARN_ON(num_ctx > NR_CTX_IDS))
		num_ctx = NR_CTX_IDS;

	while (vid_bmap) {
		/* Break if we cannot allocate more. */
		if (ctx >= num_ctx) {
			dev_warn(&pdev->dev,
				 "could not allocate all context IDs, DMA may be blocked (VID bitmap: 0x%x)",
				 vid_bmap);
			break;
		}

		vid = __ffs(vid_bmap);
		vid_bmap &= ~BIT(vid);
		ctx_vid[ctx++] = vid;
	}

	/* The following loop was unrolled so bitmasks are constant. */
	BUILD_BUG_ON(NR_CTX_IDS != 8);
	return CTX_CFG_ENTRY(0, ctx, ctx_vid[0])
	     | CTX_CFG_ENTRY(1, ctx, ctx_vid[1])
	     | CTX_CFG_ENTRY(2, ctx, ctx_vid[2])
	     | CTX_CFG_ENTRY(3, ctx, ctx_vid[3])
	     | CTX_CFG_ENTRY(4, ctx, ctx_vid[4])
	     | CTX_CFG_ENTRY(5, ctx, ctx_vid[5])
	     | CTX_CFG_ENTRY(6, ctx, ctx_vid[6])
	     | CTX_CFG_ENTRY(7, ctx, ctx_vid[7]);
}

static int s2mpu_probe_v9(struct platform_device *pdev, void __iomem *kaddr)
{
	unsigned int num_ctx;
	u32 ssmt_valid_vid_bmap, ctx_cfg_valid_vid;

	ssmt_valid_vid_bmap = ALL_VIDS_BITMAP;
	num_ctx = readl_relaxed(kaddr + REG_NS_NUM_CONTEXT) & NUM_CONTEXT_MASK;
	ctx_cfg_valid_vid = gen_ctx_cfg_valid_vid(pdev, num_ctx, ssmt_valid_vid_bmap);
	if (!ctx_cfg_valid_vid) {
		dev_err(&pdev->dev, "failed to allocate context IDs");
		return -EINVAL;
	}

	return 0;
}

/**
 * Parse interrupt information from DT and if found, register IRQ handler.
 * This is considered optional and will not fail even if the initialization is
 * unsuccessful. In that case the IRQ will remain masked.
 */
static void s2mpu_probe_irq(struct platform_device *pdev, void __iomem *kaddr)
{
	struct s2mpu_irq_info *irq_info;
	int ret, irq;

	irq = platform_get_irq_optional(pdev, 0);

	if (irq == -ENXIO)
		return; /* No IRQ specified. */

	if (irq < 0) {
		/* IRQ specified but failed to parse. */
		dev_err(&pdev->dev, "failed to parse IRQ, IRQ not enabled");
		return;
	}

	irq_info = devm_kmalloc(&pdev->dev, sizeof(*irq_info), GFP_KERNEL);
	if (!irq_info)
		return;

	*irq_info = (struct s2mpu_irq_info){
		.dev = &pdev->dev,
		.va = kaddr,
	};

	ret = devm_request_irq(&pdev->dev, irq, s2mpu_irq_handler, 0,
			       dev_name(&pdev->dev), irq_info);
	if (ret) {
		dev_err(&pdev->dev, "failed to register IRQ, IRQ not enabled");
		return;
	}
}

static int s2mpu_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *kaddr;
	size_t res_size;
	enum s2mpu_power_state power_state = S2MPU_POWER_ALWAYS_ON;
	u32 version, power_domain_id = 0;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to parse 'reg'");
		return -EINVAL;
	}

	/* devm_ioremap_resource internally calls devm_request_mem_region. */
	kaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(kaddr)) {
		dev_err(&pdev->dev, "could not ioremap resource: %ld",
			PTR_ERR(kaddr));
		return PTR_ERR(kaddr);
	}

	if (!PAGE_ALIGNED(res->start)) {
		dev_err(&pdev->dev, "base address must be page-aligned (0x%llx)",
			res->start);
		return -EINVAL;
	}

	res_size = resource_size(res);
	if (res_size != S2MPU_MMIO_SIZE) {
		dev_err(&pdev->dev,
			"unexpected device region size (expected=%u, actual=%lu)",
			S2MPU_MMIO_SIZE, res_size);
		return -EINVAL;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "power-domain-id",
				   &power_domain_id);
	if (!ret) {
		power_state = S2MPU_POWER_ON;
	} else if (ret != -EINVAL) {
		dev_err(&pdev->dev, "failed to parse power-domain-id: %d", ret);
		return ret;
	}

	/*
	 * Try to parse IRQ information. This is optional as it only affects
	 * runtime fault reporting, and therefore errors do not fail the whole
	 * driver initialization.
	 */
	s2mpu_probe_irq(pdev, kaddr);

	version = readl_relaxed(kaddr + REG_NS_VERSION);
	switch (version & VERSION_CHECK_MASK) {
	case S2MPU_VERSION_8:
		break;
	case S2MPU_VERSION_9:
		ret = s2mpu_probe_v9(pdev, kaddr);
		if (ret)
			return ret;
		break;
	default:
		dev_err(&pdev->dev, "unexpected version 0x%08x", version);
		return -EINVAL;
	}

	return 0;
}

static const struct of_device_id of_table[] = {
	{ .compatible = "google,s2mpu" },
	{},
};

static struct platform_driver of_driver = {
	.driver = {
		.name = "kvm,s2mpu",
		.of_match_table = of_table,
	},
};

int kvm_s2mpu_init(void)
{
	int ret;

	ret = platform_driver_probe(&of_driver, s2mpu_probe);
	if (ret)
		goto out;

	kvm_info("S2MPU driver initialized\n");

out:
	return ret;
}
