// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - 2026 Intel Corporation
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iopoll.h>
#include <linux/types.h>

#include "ipu6.h"
#include "ipu6-bus.h"
#include "ipu6-buttress.h"
#include "ipu6-dma.h"
#include "ipu6-platform-buttress-regs.h"
#include "ipu7-boot.h"
#include "ipu7-platform-regs.h"

#define IPU7_FW_START_STOP_TIMEOUT		2000
#define IPU7_BOOT_CELL_RESET_TIMEOUT		(2 * USEC_PER_SEC)
#define IPU7_BUTTRESS_REG_FW_BOOT_PARAMS0				0x4000
#define IPU7_BUTTRESS_FW_BOOT_PARAMS_ENTRY(i) \
	(IPU7_BUTTRESS_REG_FW_BOOT_PARAMS0 + ((i) * 4U))

struct boot_regs {
	u32 base;
	u32 dmem_address;
	u32 status_ctrl_reg;
	u32 fw_start_address_reg;
	u32 fw_code_base_reg;
};

enum ipu7_boot_reg_id {
	IPU7_FW_BOOT_CONFIG_ID = 0,
	IPU7_FW_BOOT_STATE_ID = 1,
	IPU7_FW_BOOT_SYSCOM_QUEUE_INDICES_BASE_ID = 2,
	IPU7_FW_BOOT_UNTRUSTED_ADDR_MIN_ID = 3,
	IPU7_FW_BOOT_MESSAGING_VERSION_ID = 4,
	IPU7_FW_BOOT_ID_MAX,
};

enum ipu7_boot_state {
	IPU7_FW_BOOT_STATE_UNINIT = 0x57a7e000U,
	IPU7_FW_BOOT_STATE_READY = 0x57a7e100U,
	IPU7_FW_BOOT_STATE_SHUTDOWN_CMD = 0x57a7f001U,
	IPU7_FW_BOOT_STATE_INACTIVE = 0x57a7e300U,
};

static const struct boot_regs boot_regs[IPU_SUBSYS_NUM] = {
	[IPU_ISYS] = {
		.dmem_address = IPU7_ISYS_DMEM_OFFSET,
		.status_ctrl_reg = IPU7_BUTTRESS_REG_ISYS_UCX_CTRL_STATUS,
		.fw_start_address_reg = IPU7_BUTTRESS_REG_ISYS_UCX_START_ADDR,
		.fw_code_base_reg = IPU7_IS_UC_CTRL_BASE
	},
	[IPU_PSYS] = {
		.dmem_address = IPU7_PSYS_DMEM_OFFSET,
		.status_ctrl_reg = IPU7_BUTTRESS_REG_PSYS_UCX_CTRL_STATUS,
		.fw_start_address_reg = IPU7_BUTTRESS_REG_PSYS_UCX_START_ADDR,
		.fw_code_base_reg = IPU7_PS_UC_CTRL_BASE
	}
};

static u32 get_fw_boot_reg_addr(const struct ipu6_bus_device *adev,
				enum ipu7_boot_reg_id reg)
{
	u32 base = (adev->ctrl->subsys_id == IPU_ISYS) ?
				0U : (u32)IPU7_FW_BOOT_ID_MAX;

	return IPU7_BUTTRESS_FW_BOOT_PARAMS_ENTRY(base + (u32)reg);
}

static void write_fw_boot_param(const struct ipu6_bus_device *adev,
				enum ipu7_boot_reg_id reg,
				u32 val)
{
	void __iomem *base = adev->isp->base;

	dev_dbg(&adev->auxdev.dev,
		"write boot param reg: %d addr: %x val: 0x%x\n",
		reg, get_fw_boot_reg_addr(adev, reg), val);
	writel(val, base + get_fw_boot_reg_addr(adev, reg));
}

static u32 read_fw_boot_param(const struct ipu6_bus_device *adev,
			      enum ipu7_boot_reg_id reg)
{
	void __iomem *base = adev->isp->base;

	return readl(base + get_fw_boot_reg_addr(adev, reg));
}

static int ipu7_boot_cell_reset(const struct ipu6_bus_device *adev)
{
	const struct device *dev = &adev->auxdev.dev;
	const struct boot_regs *regs = &boot_regs[adev->ctrl->subsys_id];
	u32 ucx_ctrl_status = regs->status_ctrl_reg;
	u32 timeout = IPU7_BOOT_CELL_RESET_TIMEOUT;
	void __iomem *base = adev->isp->base;
	u32 val, val2;
	int ret;

	val = readl(base + ucx_ctrl_status);
	val |= IPU7_UCX_CTL_RESET;
	val &= ~IPU7_UCX_CTL_RUN;

	writel(val, base + ucx_ctrl_status);

	ret = readl_poll_timeout(base + ucx_ctrl_status, val2,
				 (val2 & 0x3U) == (val & 0x3U), 100, timeout);
	if (ret) {
		dev_err(dev, "cell enter reset timeout. status: 0x%x\n", val2);
		return -ETIMEDOUT;
	}

	val = readl(base + ucx_ctrl_status);
	val &= ~(IPU7_UCX_CTL_RESET | IPU7_UCX_CTL_RUN);
	writel(val, base + ucx_ctrl_status);

	ret = readl_poll_timeout(base + ucx_ctrl_status, val2,
				 (val2 & 0x3U) == (val & 0x3U), 100, timeout);
	if (ret) {
		dev_err(dev, "cell exit reset timeout. status: 0x%x\n", val2);
		return -ETIMEDOUT;
	}

	return 0;
}

static void ipu7_boot_cell_start(const struct ipu6_bus_device *adev)
{
	const struct boot_regs *regs = &boot_regs[adev->ctrl->subsys_id];
	void __iomem *base = adev->isp->base;
	u32 val;

	val = readl(base + regs->status_ctrl_reg);
	WARN_ON(val & (IPU7_UCX_CTL_RESET | IPU7_UCX_CTL_RUN));

	val &= ~IPU7_UCX_CTL_RESET;
	val |= IPU7_UCX_CTL_RUN;
	writel(val, base + regs->status_ctrl_reg);
}

static void ipu7_boot_cell_stop(const struct ipu6_bus_device *adev)
{
	const struct boot_regs *regs = &boot_regs[adev->ctrl->subsys_id];
	void __iomem *base = adev->isp->base;
	u32 val;

	val = readl(base + regs->status_ctrl_reg);
	val &= ~IPU7_UCX_CTL_RUN;
	writel(val, base + regs->status_ctrl_reg);

	/* Wait for uC transactions complete */
	usleep_range(10, 20);

	val = readl(base + regs->status_ctrl_reg);
	val |= IPU7_UCX_CTL_RESET;
	writel(val, base + regs->status_ctrl_reg);
}

static int ipu7_boot_cell_init(const struct ipu6_bus_device *adev,
			       const struct ipu7_fw_com_context *fwctx)
{
	const struct boot_regs *regs = &boot_regs[adev->ctrl->subsys_id];
	void __iomem *base = adev->isp->base;

	writel(fwctx->fw_entry, base + regs->fw_start_address_reg);

	return ipu7_boot_cell_reset(adev);
}

static void init_cfg_versions(struct ipu7_boot_abi_cfg *boot_cfg, u32 length, u8 major)
{
	boot_cfg->length = length;
	boot_cfg->config_version.major = 1U;
	boot_cfg->config_version.minor = 0U;
	boot_cfg->config_version.subminor = 0U;
	boot_cfg->config_version.patch = 0U;

	boot_cfg->client_version_support.num_versions = 1U;
	boot_cfg->client_version_support.versions[0].major = major;
	boot_cfg->client_version_support.versions[0].minor = 0U;
	boot_cfg->client_version_support.versions[0].subminor = 0U;
	boot_cfg->client_version_support.versions[0].patch = 0U;
}

int ipu6_ipu7_init_boot_config(struct ipu6_bus_device *adev,
			       struct ipu7_fw_com_context *fwctx,
			       struct ipu7_fw_com_queue_config *qconfigs,
			       int num_queues, u32 uc_freq,
			       dma_addr_t subsys_config, u8 major)
{
	struct ipu7_boot_abi_cfg *boot_config;
	struct ipu7_fw_com_queue_params_config *cfgs;
	struct device *dev = &adev->auxdev.dev;
	u32 total_queue_size_aligned = 0;
	dma_addr_t queue_mem_dma_ptr;
	void *queue_mem_ptr;
	unsigned int i;

	/* Allocate boot config. */
	fwctx->boot_config_size =
		sizeof(*cfgs) * num_queues + sizeof(*boot_config);
	fwctx->boot_config = ipu6_dma_alloc(adev, fwctx->boot_config_size,
					    &fwctx->boot_config_dma_addr,
					    GFP_KERNEL, 0);
	if (!fwctx->boot_config) {
		dev_err(dev, "Failed to allocate boot config.\n");
		return -ENOMEM;
	}

	boot_config = fwctx->boot_config;
	memset(boot_config, 0, sizeof(*boot_config));
	init_cfg_versions(boot_config, fwctx->boot_config_size, major);
	boot_config->subsys_config = subsys_config;

	boot_config->uc_tile_frequency = uc_freq;
	boot_config->uc_tile_frequency_units = 0;
	boot_config->fw_com_config.max_output_queues =
		fwctx->num_output_queues;
	boot_config->fw_com_config.max_input_queues =
		fwctx->num_input_queues;

	ipu6_dma_sync_single(adev, fwctx->boot_config_dma_addr,
			     fwctx->boot_config_size);

	for (i = 0; i < num_queues; i++) {
		u32 queue_size = qconfigs[i].max_capacity *
			qconfigs[i].token_size_in_bytes;

		queue_size = ALIGN(queue_size, 64U);
		total_queue_size_aligned += queue_size;
		qconfigs[i].queue_size = queue_size;
	}

	/* Allocate queue memory */
	fwctx->queue_mem = ipu6_dma_alloc(adev, total_queue_size_aligned,
					  &fwctx->queue_mem_dma_addr,
					  GFP_KERNEL, 0);
	if (!fwctx->queue_mem) {
		dev_err(dev, "Failed to allocate queue memory.\n");
		return -ENOMEM;
	}
	fwctx->queue_mem_size = total_queue_size_aligned;

	cfgs = ipu7_fw_com_get_queue_config(&boot_config->fw_com_config);
	queue_mem_ptr = fwctx->queue_mem;
	queue_mem_dma_ptr = fwctx->queue_mem_dma_addr;
	for (i = 0; i < num_queues; i++) {
		cfgs[i].token_array_mem = queue_mem_dma_ptr;
		cfgs[i].max_capacity = qconfigs[i].max_capacity;
		cfgs[i].token_size_in_bytes = qconfigs[i].token_size_in_bytes;
		qconfigs[i].token_array_mem = queue_mem_ptr;
		queue_mem_dma_ptr += qconfigs[i].queue_size;
		queue_mem_ptr += qconfigs[i].queue_size;
	}

	ipu6_dma_sync_single(adev, fwctx->queue_mem_dma_addr,
			     total_queue_size_aligned);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu6_ipu7_init_boot_config, "INTEL_IPU6");

void ipu6_ipu7_release_boot_config(struct ipu6_bus_device *adev,
				   struct ipu7_fw_com_context *fwctx)
{
	if (!fwctx)
		return;

	if (fwctx->queue_mem) {
		ipu6_dma_free(adev, fwctx->queue_mem_size,
			      fwctx->queue_mem,
			      fwctx->queue_mem_dma_addr, 0);
		fwctx->queue_mem = NULL;
		fwctx->queue_mem_dma_addr = 0;
	}

	if (fwctx->boot_config) {
		ipu6_dma_free(adev, fwctx->boot_config_size,
			      fwctx->boot_config,
			      fwctx->boot_config_dma_addr, 0);
		fwctx->boot_config = NULL;
		fwctx->boot_config_dma_addr = 0;
	}
}
EXPORT_SYMBOL_NS_GPL(ipu6_ipu7_release_boot_config, "INTEL_IPU6");

int ipu6_ipu7_boot_start_fw(const struct ipu6_bus_device *adev,
			    struct ipu7_fw_com_context *fwctx)
{
	const struct device *dev = &adev->auxdev.dev;
	u32 timeout = IPU7_FW_START_STOP_TIMEOUT;
	void __iomem *base = adev->isp->base;
	u32 boot_state, last_boot_state;
	u32 indices_addr, msg_ver, id;
	int ret;

	ret = ipu7_boot_cell_init(adev, fwctx);
	if (ret)
		return ret;

	/* store "uninit" state to boot state reg */
	write_fw_boot_param(adev, IPU7_FW_BOOT_STATE_ID,
			    IPU7_FW_BOOT_STATE_UNINIT);
	/* Set registers to zero, recommended for diagnostics. */
	write_fw_boot_param(adev,
			    IPU7_FW_BOOT_SYSCOM_QUEUE_INDICES_BASE_ID, 0);
	write_fw_boot_param(adev, IPU7_FW_BOOT_MESSAGING_VERSION_ID, 0);
	/* store firmware configuration address */
	write_fw_boot_param(adev, IPU7_FW_BOOT_CONFIG_ID,
			    fwctx->boot_config_dma_addr);

	ipu7_boot_cell_start(adev);

	last_boot_state = IPU7_FW_BOOT_STATE_UNINIT;
	while (timeout--) {
		boot_state = read_fw_boot_param(adev,
						IPU7_FW_BOOT_STATE_ID);
		if (boot_state != last_boot_state) {
			dev_dbg(dev, "boot state changed from 0x%x to 0x%x\n",
				last_boot_state, boot_state);
			last_boot_state = boot_state;
		}
		if (IPU7_BOOT_STATE_CRITICAL(boot_state) ||
		    IPU7_BOOT_STATE_READY(boot_state))
			break;
		usleep_range(1000, 1200);
	}

	if (IPU7_BOOT_STATE_CRITICAL(boot_state)) {
		dev_err(dev, "critical boot state error 0x%x\n", boot_state);
		return -EINVAL;
	} else if (!IPU7_BOOT_STATE_READY(boot_state)) {
		dev_err(dev, "fw boot timeout. state: 0x%x\n", boot_state);
		return -ETIMEDOUT;
	}
	dev_dbg(dev, "fw boot done.\n");

	id = IPU7_FW_BOOT_SYSCOM_QUEUE_INDICES_BASE_ID;
	indices_addr = read_fw_boot_param(adev, id);
	fwctx->queue_indices = base + indices_addr;
	dev_dbg(dev, "fw queue indices offset is 0x%x\n", indices_addr);

	msg_ver = read_fw_boot_param(adev,
				     IPU7_FW_BOOT_MESSAGING_VERSION_ID);
	dev_dbg(dev, "ipu message version is 0x%08x\n", msg_ver);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu6_ipu7_boot_start_fw, "INTEL_IPU6");

int ipu6_ipu7_boot_stop_fw(const struct ipu6_bus_device *adev)
{
	const struct device *dev = &adev->auxdev.dev;
	u32 timeout = IPU7_FW_START_STOP_TIMEOUT;
	u32 boot_state;

	boot_state = read_fw_boot_param(adev, IPU7_FW_BOOT_STATE_ID);
	if (IPU7_BOOT_STATE_CRITICAL(boot_state) ||
	    !IPU7_BOOT_STATE_READY(boot_state)) {
		dev_err(dev, "fw not ready for shutdown, state 0x%x\n",
			boot_state);
		return -EBUSY;
	}

	/* Issue shutdown to start shutdown process */
	dev_dbg(dev, "stopping fw...\n");
	write_fw_boot_param(adev, IPU7_FW_BOOT_STATE_ID,
			    IPU7_FW_BOOT_STATE_SHUTDOWN_CMD);
	while (timeout--) {
		boot_state = read_fw_boot_param(adev,
						IPU7_FW_BOOT_STATE_ID);
		if (IPU7_BOOT_STATE_CRITICAL(boot_state) ||
		    IPU7_BOOT_STATE_INACTIVE(boot_state))
			break;
		usleep_range(1000, 1200);
	}

	if (IPU7_BOOT_STATE_CRITICAL(boot_state)) {
		dev_err(dev, "critical boot state error 0x%x\n", boot_state);
		return -EINVAL;
	} else if (!IPU7_BOOT_STATE_INACTIVE(boot_state)) {
		dev_err(dev, "stop fw timeout. state: 0x%x\n", boot_state);
		return -ETIMEDOUT;
	}

	ipu7_boot_cell_stop(adev);
	dev_dbg(dev, "stop fw done.\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu6_ipu7_boot_stop_fw, "INTEL_IPU6");

u32 ipu6_ipu7_boot_get_state(const struct ipu6_bus_device *adev)
{
	return read_fw_boot_param(adev, IPU7_FW_BOOT_STATE_ID);
}
EXPORT_SYMBOL_NS_GPL(ipu6_ipu7_boot_get_state, "INTEL_IPU6");
