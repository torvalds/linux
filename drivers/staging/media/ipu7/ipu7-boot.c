// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - 2025 Intel Corporation
 */

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/string.h>
#include <linux/types.h>

#include "abi/ipu7_fw_boot_abi.h"

#include "ipu7.h"
#include "ipu7-boot.h"
#include "ipu7-bus.h"
#include "ipu7-buttress-regs.h"
#include "ipu7-dma.h"
#include "ipu7-platform-regs.h"
#include "ipu7-syscom.h"

#define IPU_FW_START_STOP_TIMEOUT		2000
#define IPU_BOOT_CELL_RESET_TIMEOUT		(2 * USEC_PER_SEC)
#define BOOT_STATE_IS_CRITICAL(s)	IA_GOFO_FW_BOOT_STATE_IS_CRITICAL(s)
#define BOOT_STATE_IS_READY(s)		((s) == IA_GOFO_FW_BOOT_STATE_READY)
#define BOOT_STATE_IS_INACTIVE(s)	((s) == IA_GOFO_FW_BOOT_STATE_INACTIVE)

struct ipu7_boot_context {
	u32 base;
	u32 dmem_address;
	u32 status_ctrl_reg;
	u32 fw_start_address_reg;
	u32 fw_code_base_reg;
};

static const struct ipu7_boot_context contexts[IPU_SUBSYS_NUM] = {
	{
		/* ISYS */
		.dmem_address = IPU_ISYS_DMEM_OFFSET,
		.status_ctrl_reg = BUTTRESS_REG_DRV_IS_UCX_CONTROL_STATUS,
		.fw_start_address_reg = BUTTRESS_REG_DRV_IS_UCX_START_ADDR,
		.fw_code_base_reg = IS_UC_CTRL_BASE
	},
	{
		/* PSYS */
		.dmem_address = IPU_PSYS_DMEM_OFFSET,
		.status_ctrl_reg = BUTTRESS_REG_DRV_PS_UCX_CONTROL_STATUS,
		.fw_start_address_reg = BUTTRESS_REG_DRV_PS_UCX_START_ADDR,
		.fw_code_base_reg = PS_UC_CTRL_BASE
	}
};

static u32 get_fw_boot_reg_addr(const struct ipu7_bus_device *adev,
				enum ia_gofo_buttress_reg_id reg)
{
	u32 base = (adev->subsys == IPU_IS) ? 0U : (u32)IA_GOFO_FW_BOOT_ID_MAX;

	return BUTTRESS_FW_BOOT_PARAMS_ENTRY(base + (u32)reg);
}

static void write_fw_boot_param(const struct ipu7_bus_device *adev,
				enum ia_gofo_buttress_reg_id reg,
				u32 val)
{
	void __iomem *base = adev->isp->base;

	dev_dbg(&adev->auxdev.dev,
		"write boot param reg: %d addr: %x val: 0x%x\n",
		reg, get_fw_boot_reg_addr(adev, reg), val);
	writel(val, base + get_fw_boot_reg_addr(adev, reg));
}

static u32 read_fw_boot_param(const struct ipu7_bus_device *adev,
			      enum ia_gofo_buttress_reg_id reg)
{
	void __iomem *base = adev->isp->base;

	return readl(base + get_fw_boot_reg_addr(adev, reg));
}

static int ipu7_boot_cell_reset(const struct ipu7_bus_device *adev)
{
	const struct ipu7_boot_context *ctx = &contexts[adev->subsys];
	const struct device *dev = &adev->auxdev.dev;
	u32 ucx_ctrl_status = ctx->status_ctrl_reg;
	u32 timeout = IPU_BOOT_CELL_RESET_TIMEOUT;
	void __iomem *base = adev->isp->base;
	u32 val, val2;
	int ret;

	dev_dbg(dev, "cell enter reset...\n");
	val = readl(base + ucx_ctrl_status);
	dev_dbg(dev, "cell_ctrl_reg addr = 0x%x, val = 0x%x\n",
		ucx_ctrl_status, val);

	dev_dbg(dev, "force cell reset...\n");
	val |= UCX_CTL_RESET;
	val &= ~UCX_CTL_RUN;

	dev_dbg(dev, "write status_ctrl_reg(0x%x) to 0x%x\n",
		ucx_ctrl_status, val);
	writel(val, base + ucx_ctrl_status);

	ret = readl_poll_timeout(base + ucx_ctrl_status, val2,
				 (val2 & 0x3U) == (val & 0x3U), 100, timeout);
	if (ret) {
		dev_err(dev, "cell enter reset timeout. status: 0x%x\n", val2);
		return -ETIMEDOUT;
	}

	dev_dbg(dev, "cell exit reset...\n");
	val = readl(base + ucx_ctrl_status);
	WARN((!(val & UCX_CTL_RESET) || val & UCX_CTL_RUN),
	     "cell status 0x%x", val);

	val &= ~(UCX_CTL_RESET | UCX_CTL_RUN);
	dev_dbg(dev, "write status_ctrl_reg(0x%x) to 0x%x\n",
		ucx_ctrl_status, val);
	writel(val, base + ucx_ctrl_status);

	ret = readl_poll_timeout(base + ucx_ctrl_status, val2,
				 (val2 & 0x3U) == (val & 0x3U), 100, timeout);
	if (ret) {
		dev_err(dev, "cell exit reset timeout. status: 0x%x\n", val2);
		return -ETIMEDOUT;
	}

	return 0;
}

static void ipu7_boot_cell_start(const struct ipu7_bus_device *adev)
{
	const struct ipu7_boot_context *ctx = &contexts[adev->subsys];
	void __iomem *base = adev->isp->base;
	const struct device *dev = &adev->auxdev.dev;
	u32 val;

	dev_dbg(dev, "starting cell...\n");
	val = readl(base + ctx->status_ctrl_reg);
	WARN_ON(val & (UCX_CTL_RESET | UCX_CTL_RUN));

	val &= ~UCX_CTL_RESET;
	val |= UCX_CTL_RUN;
	dev_dbg(dev, "write status_ctrl_reg(0x%x) to 0x%x\n",
		ctx->status_ctrl_reg, val);
	writel(val, base + ctx->status_ctrl_reg);
}

static void ipu7_boot_cell_stop(const struct ipu7_bus_device *adev)
{
	const struct ipu7_boot_context *ctx = &contexts[adev->subsys];
	void __iomem *base = adev->isp->base;
	const struct device *dev = &adev->auxdev.dev;
	u32 val;

	dev_dbg(dev, "stopping cell...\n");

	val = readl(base + ctx->status_ctrl_reg);
	val &= ~UCX_CTL_RUN;
	dev_dbg(dev, "write status_ctrl_reg(0x%x) to 0x%x\n",
		ctx->status_ctrl_reg, val);
	writel(val, base + ctx->status_ctrl_reg);

	/* Wait for uC transactions complete */
	usleep_range(10, 20);

	val = readl(base + ctx->status_ctrl_reg);
	val |= UCX_CTL_RESET;
	dev_dbg(dev, "write status_ctrl_reg(0x%x) to 0x%x\n",
		ctx->status_ctrl_reg, val);
	writel(val, base + ctx->status_ctrl_reg);
}

static int ipu7_boot_cell_init(const struct ipu7_bus_device *adev)
{
	const struct ipu7_boot_context *ctx = &contexts[adev->subsys];
	void __iomem *base = adev->isp->base;

	dev_dbg(&adev->auxdev.dev, "write fw_start_address_reg(0x%x) to 0x%x\n",
		ctx->fw_start_address_reg, adev->fw_entry);
	writel(adev->fw_entry, base + ctx->fw_start_address_reg);

	return ipu7_boot_cell_reset(adev);
}

static void init_boot_config(struct ia_gofo_boot_config *boot_config,
			     u32 length, u8 major)
{
	/* syscom version, new syscom2 version */
	boot_config->length = length;
	boot_config->config_version.major = 1U;
	boot_config->config_version.minor = 0U;
	boot_config->config_version.subminor = 0U;
	boot_config->config_version.patch = 0U;

	/* msg version for task interface */
	boot_config->client_version_support.num_versions = 1U;
	boot_config->client_version_support.versions[0].major = major;
	boot_config->client_version_support.versions[0].minor = 0U;
	boot_config->client_version_support.versions[0].subminor = 0U;
	boot_config->client_version_support.versions[0].patch = 0U;
}

int ipu7_boot_init_boot_config(struct ipu7_bus_device *adev,
			       struct syscom_queue_config *qconfigs,
			       int num_queues, u32 uc_freq,
			       dma_addr_t subsys_config, u8 major)
{
	u32 total_queue_size_aligned = 0;
	struct ipu7_syscom_context *syscom = adev->syscom;
	struct ia_gofo_boot_config *boot_config;
	struct syscom_queue_params_config *cfgs;
	struct device *dev = &adev->auxdev.dev;
	struct syscom_config_s *syscfg;
	dma_addr_t queue_mem_dma_ptr;
	void *queue_mem_ptr;
	unsigned int i;

	dev_dbg(dev, "boot config queues_nr: %d freq: %u sys_conf: 0x%pad\n",
		num_queues, uc_freq, &subsys_config);
	/* Allocate boot config. */
	adev->boot_config_size =
		sizeof(*cfgs) * num_queues + sizeof(*boot_config);
	adev->boot_config = ipu7_dma_alloc(adev, adev->boot_config_size,
					   &adev->boot_config_dma_addr,
					   GFP_KERNEL, 0);
	if (!adev->boot_config) {
		dev_err(dev, "Failed to allocate boot config.\n");
		return -ENOMEM;
	}

	boot_config = adev->boot_config;
	memset(boot_config, 0, sizeof(struct ia_gofo_boot_config));
	init_boot_config(boot_config, adev->boot_config_size, major);
	boot_config->subsys_config = subsys_config;

	boot_config->uc_tile_frequency = uc_freq;
	boot_config->uc_tile_frequency_units =
		IA_GOFO_FW_BOOT_UC_FREQUENCY_UNITS_MHZ;
	boot_config->syscom_context_config.max_output_queues =
		syscom->num_output_queues;
	boot_config->syscom_context_config.max_input_queues =
		syscom->num_input_queues;

	ipu7_dma_sync_single(adev, adev->boot_config_dma_addr,
			     adev->boot_config_size);

	for (i = 0; i < num_queues; i++) {
		u32 queue_size = qconfigs[i].max_capacity *
			qconfigs[i].token_size_in_bytes;

		queue_size = ALIGN(queue_size, 64U);
		total_queue_size_aligned += queue_size;
		qconfigs[i].queue_size = queue_size;
	}

	/* Allocate queue memory */
	syscom->queue_mem = ipu7_dma_alloc(adev, total_queue_size_aligned,
					   &syscom->queue_mem_dma_addr,
					   GFP_KERNEL, 0);
	if (!syscom->queue_mem) {
		dev_err(dev, "Failed to allocate queue memory.\n");
		return -ENOMEM;
	}
	syscom->queue_mem_size = total_queue_size_aligned;

	syscfg = &boot_config->syscom_context_config;
	cfgs = ipu7_syscom_get_queue_config(syscfg);
	queue_mem_ptr = syscom->queue_mem;
	queue_mem_dma_ptr = syscom->queue_mem_dma_addr;
	for (i = 0; i < num_queues; i++) {
		cfgs[i].token_array_mem = queue_mem_dma_ptr;
		cfgs[i].max_capacity = qconfigs[i].max_capacity;
		cfgs[i].token_size_in_bytes = qconfigs[i].token_size_in_bytes;
		qconfigs[i].token_array_mem = queue_mem_ptr;
		queue_mem_dma_ptr += qconfigs[i].queue_size;
		queue_mem_ptr += qconfigs[i].queue_size;
	}

	ipu7_dma_sync_single(adev, syscom->queue_mem_dma_addr,
			     total_queue_size_aligned);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu7_boot_init_boot_config, "INTEL_IPU7");

void ipu7_boot_release_boot_config(struct ipu7_bus_device *adev)
{
	struct ipu7_syscom_context *syscom = adev->syscom;

	if (syscom->queue_mem) {
		ipu7_dma_free(adev, syscom->queue_mem_size,
			      syscom->queue_mem,
			      syscom->queue_mem_dma_addr, 0);
		syscom->queue_mem = NULL;
		syscom->queue_mem_dma_addr = 0;
	}

	if (adev->boot_config) {
		ipu7_dma_free(adev, adev->boot_config_size,
			      adev->boot_config,
			      adev->boot_config_dma_addr, 0);
		adev->boot_config = NULL;
		adev->boot_config_dma_addr = 0;
	}
}
EXPORT_SYMBOL_NS_GPL(ipu7_boot_release_boot_config, "INTEL_IPU7");

int ipu7_boot_start_fw(const struct ipu7_bus_device *adev)
{
	const struct device *dev = &adev->auxdev.dev;
	u32 timeout = IPU_FW_START_STOP_TIMEOUT;
	void __iomem *base = adev->isp->base;
	u32 boot_state, last_boot_state;
	u32 indices_addr, msg_ver, id;
	int ret;

	ret = ipu7_boot_cell_init(adev);
	if (ret)
		return ret;

	dev_dbg(dev, "start booting fw...\n");
	/* store "uninit" state to syscom/boot state reg */
	write_fw_boot_param(adev, IA_GOFO_FW_BOOT_STATE_ID,
			    IA_GOFO_FW_BOOT_STATE_UNINIT);
	/*
	 * Set registers to zero
	 * (not strictly required, but recommended for diagnostics)
	 */
	write_fw_boot_param(adev,
			    IA_GOFO_FW_BOOT_SYSCOM_QUEUE_INDICES_BASE_ID, 0);
	write_fw_boot_param(adev, IA_GOFO_FW_BOOT_MESSAGING_VERSION_ID, 0);
	/* store firmware configuration address */
	write_fw_boot_param(adev, IA_GOFO_FW_BOOT_CONFIG_ID,
			    adev->boot_config_dma_addr);

	/* Kick uC, then wait for boot complete */
	ipu7_boot_cell_start(adev);

	last_boot_state = IA_GOFO_FW_BOOT_STATE_UNINIT;
	while (timeout--) {
		boot_state = read_fw_boot_param(adev,
						IA_GOFO_FW_BOOT_STATE_ID);
		if (boot_state != last_boot_state) {
			dev_dbg(dev, "boot state changed from 0x%x to 0x%x\n",
				last_boot_state, boot_state);
			last_boot_state = boot_state;
		}
		if (BOOT_STATE_IS_CRITICAL(boot_state) ||
		    BOOT_STATE_IS_READY(boot_state))
			break;
		usleep_range(1000, 1200);
	}

	if (BOOT_STATE_IS_CRITICAL(boot_state)) {
		ipu7_dump_fw_error_log(adev);
		dev_err(dev, "critical boot state error 0x%x\n", boot_state);
		return -EINVAL;
	} else if (!BOOT_STATE_IS_READY(boot_state)) {
		dev_err(dev, "fw boot timeout. state: 0x%x\n", boot_state);
		return -ETIMEDOUT;
	}
	dev_dbg(dev, "fw boot done.\n");

	/* Get FW syscom queue indices addr */
	id = IA_GOFO_FW_BOOT_SYSCOM_QUEUE_INDICES_BASE_ID;
	indices_addr = read_fw_boot_param(adev, id);
	adev->syscom->queue_indices = base + indices_addr;
	dev_dbg(dev, "fw queue indices offset is 0x%x\n", indices_addr);

	/* Get message version. */
	msg_ver = read_fw_boot_param(adev,
				     IA_GOFO_FW_BOOT_MESSAGING_VERSION_ID);
	dev_dbg(dev, "ipu message version is 0x%08x\n", msg_ver);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu7_boot_start_fw, "INTEL_IPU7");

int ipu7_boot_stop_fw(const struct ipu7_bus_device *adev)
{
	const struct device *dev = &adev->auxdev.dev;
	u32 timeout = IPU_FW_START_STOP_TIMEOUT;
	u32 boot_state;

	boot_state = read_fw_boot_param(adev, IA_GOFO_FW_BOOT_STATE_ID);
	if (BOOT_STATE_IS_CRITICAL(boot_state) ||
	    !BOOT_STATE_IS_READY(boot_state)) {
		dev_err(dev, "fw not ready for shutdown, state 0x%x\n",
			boot_state);
		return -EBUSY;
	}

	/* Issue shutdown to start shutdown process */
	dev_dbg(dev, "stopping fw...\n");
	write_fw_boot_param(adev, IA_GOFO_FW_BOOT_STATE_ID,
			    IA_GOFO_FW_BOOT_STATE_SHUTDOWN_CMD);
	while (timeout--) {
		boot_state = read_fw_boot_param(adev,
						IA_GOFO_FW_BOOT_STATE_ID);
		if (BOOT_STATE_IS_CRITICAL(boot_state) ||
		    BOOT_STATE_IS_INACTIVE(boot_state))
			break;
		usleep_range(1000, 1200);
	}

	if (BOOT_STATE_IS_CRITICAL(boot_state)) {
		ipu7_dump_fw_error_log(adev);
		dev_err(dev, "critical boot state error 0x%x\n", boot_state);
		return -EINVAL;
	} else if (!BOOT_STATE_IS_INACTIVE(boot_state)) {
		dev_err(dev, "stop fw timeout. state: 0x%x\n", boot_state);
		return -ETIMEDOUT;
	}

	ipu7_boot_cell_stop(adev);
	dev_dbg(dev, "stop fw done.\n");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu7_boot_stop_fw, "INTEL_IPU7");

u32 ipu7_boot_get_boot_state(const struct ipu7_bus_device *adev)
{
	return read_fw_boot_param(adev, IA_GOFO_FW_BOOT_STATE_ID);
}
EXPORT_SYMBOL_NS_GPL(ipu7_boot_get_boot_state, "INTEL_IPU7");
