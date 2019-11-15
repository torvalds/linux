// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/qcom_scm.h>
#include <linux/sizes.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "core.h"
#include "firmware.h"
#include "hfi_venus_io.h"

#define VENUS_PAS_ID			9
#define VENUS_FW_MEM_SIZE		(6 * SZ_1M)
#define VENUS_FW_START_ADDR		0x0

static void venus_reset_cpu(struct venus_core *core)
{
	u32 fw_size = core->fw.mapped_mem_size;
	void __iomem *base = core->base;

	writel(0, base + WRAPPER_FW_START_ADDR);
	writel(fw_size, base + WRAPPER_FW_END_ADDR);
	writel(0, base + WRAPPER_CPA_START_ADDR);
	writel(fw_size, base + WRAPPER_CPA_END_ADDR);
	writel(fw_size, base + WRAPPER_NONPIX_START_ADDR);
	writel(fw_size, base + WRAPPER_NONPIX_END_ADDR);
	writel(0x0, base + WRAPPER_CPU_CGC_DIS);
	writel(0x0, base + WRAPPER_CPU_CLOCK_CONFIG);

	/* Bring ARM9 out of reset */
	writel(0, base + WRAPPER_A9SS_SW_RESET);
}

int venus_set_hw_state(struct venus_core *core, bool resume)
{
	if (core->use_tz)
		return qcom_scm_set_remote_state(resume, 0);

	if (resume)
		venus_reset_cpu(core);
	else
		writel(1, core->base + WRAPPER_A9SS_SW_RESET);

	return 0;
}

static int venus_load_fw(struct venus_core *core, const char *fwname,
			 phys_addr_t *mem_phys, size_t *mem_size)
{
	const struct firmware *mdt;
	struct device_node *node;
	struct device *dev;
	struct resource r;
	ssize_t fw_size;
	void *mem_va;
	int ret;

	*mem_phys = 0;
	*mem_size = 0;

	dev = core->dev;
	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!node) {
		dev_err(dev, "no memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	if (ret)
		goto err_put_node;

	ret = request_firmware(&mdt, fwname, dev);
	if (ret < 0)
		goto err_put_node;

	fw_size = qcom_mdt_get_size(mdt);
	if (fw_size < 0) {
		ret = fw_size;
		goto err_release_fw;
	}

	*mem_phys = r.start;
	*mem_size = resource_size(&r);

	if (*mem_size < fw_size || fw_size > VENUS_FW_MEM_SIZE) {
		ret = -EINVAL;
		goto err_release_fw;
	}

	mem_va = memremap(r.start, *mem_size, MEMREMAP_WC);
	if (!mem_va) {
		dev_err(dev, "unable to map memory region: %pR\n", &r);
		ret = -ENOMEM;
		goto err_release_fw;
	}

	if (core->use_tz)
		ret = qcom_mdt_load(dev, mdt, fwname, VENUS_PAS_ID,
				    mem_va, *mem_phys, *mem_size, NULL);
	else
		ret = qcom_mdt_load_no_init(dev, mdt, fwname, VENUS_PAS_ID,
					    mem_va, *mem_phys, *mem_size, NULL);

	memunmap(mem_va);
err_release_fw:
	release_firmware(mdt);
err_put_node:
	of_node_put(node);
	return ret;
}

static int venus_boot_no_tz(struct venus_core *core, phys_addr_t mem_phys,
			    size_t mem_size)
{
	struct iommu_domain *iommu;
	struct device *dev;
	int ret;

	dev = core->fw.dev;
	if (!dev)
		return -EPROBE_DEFER;

	iommu = core->fw.iommu_domain;
	core->fw.mapped_mem_size = mem_size;

	ret = iommu_map(iommu, VENUS_FW_START_ADDR, mem_phys, mem_size,
			IOMMU_READ | IOMMU_WRITE | IOMMU_PRIV);
	if (ret) {
		dev_err(dev, "could not map video firmware region\n");
		return ret;
	}

	venus_reset_cpu(core);

	return 0;
}

static int venus_shutdown_no_tz(struct venus_core *core)
{
	const size_t mapped = core->fw.mapped_mem_size;
	struct iommu_domain *iommu;
	size_t unmapped;
	u32 reg;
	struct device *dev = core->fw.dev;
	void __iomem *base = core->base;

	/* Assert the reset to ARM9 */
	reg = readl_relaxed(base + WRAPPER_A9SS_SW_RESET);
	reg |= WRAPPER_A9SS_SW_RESET_BIT;
	writel_relaxed(reg, base + WRAPPER_A9SS_SW_RESET);

	/* Make sure reset is asserted before the mapping is removed */
	mb();

	iommu = core->fw.iommu_domain;

	unmapped = iommu_unmap(iommu, VENUS_FW_START_ADDR, mapped);
	if (unmapped != mapped)
		dev_err(dev, "failed to unmap firmware\n");

	return 0;
}

int venus_boot(struct venus_core *core)
{
	struct device *dev = core->dev;
	phys_addr_t mem_phys;
	size_t mem_size;
	int ret;

	if (!IS_ENABLED(CONFIG_QCOM_MDT_LOADER) ||
	    (core->use_tz && !qcom_scm_is_available()))
		return -EPROBE_DEFER;

	ret = venus_load_fw(core, core->res->fwname, &mem_phys, &mem_size);
	if (ret) {
		dev_err(dev, "fail to load video firmware\n");
		return -EINVAL;
	}

	if (core->use_tz)
		ret = qcom_scm_pas_auth_and_reset(VENUS_PAS_ID);
	else
		ret = venus_boot_no_tz(core, mem_phys, mem_size);

	return ret;
}

int venus_shutdown(struct venus_core *core)
{
	int ret;

	if (core->use_tz)
		ret = qcom_scm_pas_shutdown(VENUS_PAS_ID);
	else
		ret = venus_shutdown_no_tz(core);

	return ret;
}

int venus_firmware_init(struct venus_core *core)
{
	struct platform_device_info info;
	struct iommu_domain *iommu_dom;
	struct platform_device *pdev;
	struct device_node *np;
	int ret;

	np = of_get_child_by_name(core->dev->of_node, "video-firmware");
	if (!np) {
		core->use_tz = true;
		return 0;
	}

	memset(&info, 0, sizeof(info));
	info.fwnode = &np->fwnode;
	info.parent = core->dev;
	info.name = np->name;
	info.dma_mask = DMA_BIT_MASK(32);

	pdev = platform_device_register_full(&info);
	if (IS_ERR(pdev)) {
		of_node_put(np);
		return PTR_ERR(pdev);
	}

	pdev->dev.of_node = np;

	ret = of_dma_configure(&pdev->dev, np, true);
	if (ret) {
		dev_err(core->dev, "dma configure fail\n");
		goto err_unregister;
	}

	core->fw.dev = &pdev->dev;

	iommu_dom = iommu_domain_alloc(&platform_bus_type);
	if (!iommu_dom) {
		dev_err(core->fw.dev, "Failed to allocate iommu domain\n");
		ret = -ENOMEM;
		goto err_unregister;
	}

	ret = iommu_attach_device(iommu_dom, core->fw.dev);
	if (ret) {
		dev_err(core->fw.dev, "could not attach device\n");
		goto err_iommu_free;
	}

	core->fw.iommu_domain = iommu_dom;

	of_node_put(np);

	return 0;

err_iommu_free:
	iommu_domain_free(iommu_dom);
err_unregister:
	platform_device_unregister(pdev);
	of_node_put(np);
	return ret;
}

void venus_firmware_deinit(struct venus_core *core)
{
	struct iommu_domain *iommu;

	if (!core->fw.dev)
		return;

	iommu = core->fw.iommu_domain;

	iommu_detach_device(iommu, core->fw.dev);
	iommu_domain_free(iommu);

	platform_device_unregister(to_platform_device(core->fw.dev));
}
