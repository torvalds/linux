// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci-ats.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include <media/ipu-bridge.h>
#include <media/ipu6-pci-table.h>

#include "ipu6.h"
#include "ipu6-bus.h"
#include "ipu6-buttress.h"
#include "ipu6-cpd.h"
#include "ipu6-dma.h"
#include "ipu6-isys.h"
#include "ipu6-mmu.h"
#include "ipu6-platform-buttress-regs.h"
#include "ipu6-platform-isys-csi2-reg.h"
#include "ipu6-platform-regs.h"
#include "ipu7-isys-csi2-regs.h"

#define IPU6_PCI_BAR		0
#define IPU7_PCI_PBBAR		4

static int force_no_probe_ipu7 = !IS_BUILTIN(CONFIG_VIDEO_INTEL_IPU6_IPU7);
module_param(force_no_probe_ipu7, int, 0644);
MODULE_PARM_DESC(force_no_probe_ipu7, "Don't probe ipu7 and ipu7.5 devices");

struct ipu6_cell_program {
	u32 magic_number;

	u32 blob_offset;
	u32 blob_size;

	u32 start[3];

	u32 icache_source;
	u32 icache_target;
	u32 icache_size;

	u32 pmem_source;
	u32 pmem_target;
	u32 pmem_size;

	u32 data_source;
	u32 data_target;
	u32 data_size;

	u32 bss_target;
	u32 bss_size;

	u32 cell_id;
	u32 regs_addr;

	u32 cell_pmem_data_bus_address;
	u32 cell_dmem_data_bus_address;
	u32 cell_pmem_control_bus_address;
	u32 cell_dmem_control_bus_address;

	u32 next;
	u32 dummy[2];
};

static struct ipu6_isys_internal_pdata isys_ipdata = {
	.hw_variant = {
		.offset = IPU6_UNIFIED_OFFSET,
		.cdc_fifos = 3,
		.cdc_fifo_threshold = {6, 8, 2},
		.dmem_offset = IPU6_ISYS_DMEM_OFFSET,
		.spc_offset = IPU6_ISYS_SPC_OFFSET,
	},
	.isys_dma_overshoot = IPU6_ISYS_OVERALLOC_MIN,
};

static struct ipu6_psys_internal_pdata psys_ipdata = {
	.hw_variant = {
		.offset = IPU6_UNIFIED_OFFSET,
		.dmem_offset = IPU6_PSYS_DMEM_OFFSET,
	},
};

static const struct ipu6_buttress_ctrl ipu6_isys_buttress_ctrl = {
	.subsys_id = IPU_ISYS,
	.ratio = IPU6_IS_FREQ_CTL_DEFAULT_RATIO,
	.qos_floor = IPU6_IS_FREQ_CTL_DEFAULT_QOS_FLOOR_RATIO,
	.freq_ctl = IPU6_BUTTRESS_REG_IS_FREQ_CTL,
	.pwr_sts_shift = IPU6_BUTTRESS_PWR_STATE_IS_PWR_SHIFT,
	.pwr_sts_mask = IPU6_BUTTRESS_PWR_STATE_IS_PWR_MASK,
	.pwr_sts_on = IPU6_BUTTRESS_PWR_STATE_UP_DONE,
	.pwr_sts_off = IPU6_BUTTRESS_PWR_STATE_DN_DONE,
};

static const struct ipu6_buttress_ctrl ipu6_psys_buttress_ctrl = {
	.subsys_id = IPU_PSYS,
	.ratio = IPU6_PS_FREQ_CTL_DEFAULT_RATIO,
	.qos_floor = IPU6_PS_FREQ_CTL_DEFAULT_QOS_FLOOR_RATIO,
	.freq_ctl = IPU6_BUTTRESS_REG_PS_FREQ_CTL,
	.pwr_sts_shift = IPU6_BUTTRESS_PWR_STATE_PS_PWR_SHIFT,
	.pwr_sts_mask = IPU6_BUTTRESS_PWR_STATE_PS_PWR_MASK,
	.pwr_sts_on = IPU6_BUTTRESS_PWR_STATE_UP_DONE,
	.pwr_sts_off = IPU6_BUTTRESS_PWR_STATE_DN_DONE,
};

static const struct ipu6_buttress_ctrl ipu7_isys_buttress_ctrl = {
	.subsys_id = IPU_ISYS,
	.ratio = IPU7_IS_FREQ_CTL_DEFAULT_RATIO,
	.qos_floor = 0,
	.freq_ctl = IPU7_BUTTRESS_REG_IS_WORKPOINT_REQ,
	.pwr_sts_shift = IPU7_BUTTRESS_PWR_STATE_IS_PWR_SHIFT,
	.pwr_sts_mask = IPU7_BUTTRESS_PWR_STATE_IS_PWR_MASK,
	.pwr_sts_on = IPU6_BUTTRESS_PWR_STATE_UP_DONE,
	.pwr_sts_off = IPU6_BUTTRESS_PWR_STATE_DN_DONE,
};

static const struct ipu6_buttress_ctrl ipu7_psys_buttress_ctrl = {
	.subsys_id = IPU_PSYS,
	.ratio = IPU7_PS_FREQ_CTL_DEFAULT_RATIO,
	.qos_floor = 0,
	.freq_ctl = IPU7_BUTTRESS_REG_PS_WORKPOINT_REQ,
	.pwr_sts_shift = IPU7_BUTTRESS_PWR_STATE_PS_PWR_SHIFT,
	.pwr_sts_mask = IPU7_BUTTRESS_PWR_STATE_PS_PWR_MASK,
	.pwr_sts_on = IPU6_BUTTRESS_PWR_STATE_UP_DONE,
	.pwr_sts_off = IPU6_BUTTRESS_PWR_STATE_DN_DONE,
};

static const struct ipu6_buttress_ctrl ipu8_isys_buttress_ctrl = {
	.subsys_id = IPU_ISYS,
	.ratio = IPU8_IS_FREQ_CTL_DEFAULT_RATIO,
	.qos_floor = 0,
	.freq_ctl = IPU7_BUTTRESS_REG_IS_WORKPOINT_REQ,
	.pwr_sts_shift = IPU7_BUTTRESS_PWR_STATE_IS_PWR_SHIFT,
	.pwr_sts_mask = IPU7_BUTTRESS_PWR_STATE_IS_PWR_MASK,
	.pwr_sts_on = IPU6_BUTTRESS_PWR_STATE_UP_DONE,
	.pwr_sts_off = IPU6_BUTTRESS_PWR_STATE_DN_DONE,
};

static const struct ipu6_buttress_ctrl ipu8_psys_buttress_ctrl = {
	.subsys_id = IPU_PSYS,
	.ratio = IPU8_PS_FREQ_CTL_DEFAULT_RATIO,
	.qos_floor = 0,
	.freq_ctl = IPU7_BUTTRESS_REG_PS_WORKPOINT_REQ,
	.pwr_sts_shift = IPU7_BUTTRESS_PWR_STATE_PS_PWR_SHIFT,
	.pwr_sts_mask = IPU7_BUTTRESS_PWR_STATE_PS_PWR_MASK,
	.pwr_sts_on = IPU6_BUTTRESS_PWR_STATE_UP_DONE,
	.pwr_sts_off = IPU6_BUTTRESS_PWR_STATE_DN_DONE,
};

static const struct ipu6_buttress_registers ipu6_buttress_regs = {
	/* Registers */
	.irq_status	= BUTTRESS_REG_ISR_STATUS,
	.irq_clear	= BUTTRESS_REG_ISR_CLEAR,
	.irq_enable	= BUTTRESS_REG_ISR_ENABLE,
	.pwr_status	= BUTTRESS_REG_PWR_STATE,
	.security_ctl	= BUTTRESS_REG_SECURITY_CTL,
	.fw_reset_ctl	= BUTTRESS_REG_FW_RESET_CTL,
	.fabric_cmd	= BUTTRESS_REG_FABRIC_CMD,
	.tsw_ctl	= BUTTRESS_REG_TSW_CTL,
	.tsc_lo		= BUTTRESS_REG_TSC_LO,
	.wdt		= BUTTRESS_REG_WDT,
	.btrs_ctrl	= BUTTRESS_REG_BTRS_CTRL,
	.csr_in		= BUTTRESS_REG_CSE2IUCSR,
	.csr_out	= BUTTRESS_REG_IU2CSECSR,
	.db0_in		= BUTTRESS_REG_CSE2IUDB0,
	.db0_out	= BUTTRESS_REG_IU2CSEDB0,
	.data0_in	= BUTTRESS_REG_CSE2IUDATA0,
	.data0_out	= BUTTRESS_REG_IU2CSEDATA0,
	.sku_id		= BUTTRESS_REG_SKU,

	/* Bitmasks */
	.irq_is		= BUTTRESS_ISR_IS_IRQ,
	.irq_ps		= BUTTRESS_ISR_PS_IRQ,
	.irq_all	= BUTTRESS_IRQS,
	.irq_events	= BUTTRESS_EVENT,
	.irq_cse_ipc	= BUTTRESS_ISR_IPC_FROM_CSE_IS_WAITING,
	.irq_exec_done	= BUTTRESS_ISR_IPC_EXEC_DONE_BY_CSE,
	.irq_sai	= BUTTRESS_ISR_SAI_VIOLATION,
};

static const struct ipu6_buttress_registers ipu7_buttress_regs = {
	/* Registers */
	.irq_status	= IPU7_BUTTRESS_REG_IRQ_STATUS,
	.irq_clear	= IPU7_BUTTRESS_REG_IRQ_CLEAR,
	.irq_enable	= IPU7_BUTTRESS_REG_IRQ_ENABLE,
	.pwr_status	= IPU7_BUTTRESS_REG_PWR_STATUS,
	.security_ctl	= IPU7_BUTTRESS_REG_SECURITY_CTL,
	.fw_reset_ctl	= IPU7_BUTTRESS_REG_FW_RESET_CTL,
	.fabric_cmd	= IPU7_BUTTRESS_REG_TSC_CMD,
	.tsw_ctl	= IPU7_BUTTRESS_REG_TSC_CTL,
	.tsc_lo		= IPU7_BUTTRESS_REG_TSC_LO,
	.wdt		= IPU7_BUTTRESS_REG_IDLE_WDT,
	.csr_in		= IPU7_BUTTRESS_REG_CSE2IUCSR,
	.csr_out	= IPU7_BUTTRESS_REG_IU2CSECSR,
	.db0_in		= IPU7_BUTTRESS_REG_CSE2IUDB0,
	.db0_out	= IPU7_BUTTRESS_REG_IU2CSEDB0,
	.data0_in	= IPU7_BUTTRESS_REG_CSE2IUDATA0,
	.data0_out	= IPU7_BUTTRESS_REG_IU2CSEDATA0,
	.sku_id		= IPU7_BUTTRESS_REG_SKU,

	/* Bitmasks */
	.irq_is		= IPU7_BUTTRESS_IRQ_IS_IRQ,
	.irq_ps		= IPU7_BUTTRESS_IRQ_PS_IRQ,
	.irq_all	= IPU7_BUTTRESS_IRQS,
	.irq_events	= IPU7_BUTTRESS_IRQS,
	.irq_cse_ipc	= IPU7_BUTTRESS_IRQ_IPC_FROM_CSE_IS_WAITING,
	.irq_exec_done	= IPU7_BUTTRESS_IRQ_IPC_EXEC_DONE_BY_CSE,
	.irq_sai	= IPU7_BUTTRESS_IRQ_SAI_VIOLATION,
};

static const struct ipu6_buttress_registers ipu7p5_buttress_regs = {
	/* Registers */
	.irq_status	= IPU7_BUTTRESS_REG_IRQ_STATUS,
	.irq_clear	= IPU7_BUTTRESS_REG_IRQ_CLEAR,
	.irq_enable	= IPU7_BUTTRESS_REG_IRQ_ENABLE,
	.pwr_status	= IPU7_BUTTRESS_REG_PWR_STATUS,
	.security_ctl	= IPU7_BUTTRESS_REG_SECURITY_CTL,
	.fw_reset_ctl	= IPU7_BUTTRESS_REG_FW_RESET_CTL,
	.fabric_cmd	= IPU7_BUTTRESS_REG_TSC_CMD,
	.tsw_ctl	= IPU7_BUTTRESS_REG_TSC_CTL,
	.tsc_lo		= IPU7_BUTTRESS_REG_PB_TIMESTAMP_LO,
	.wdt		= IPU7_BUTTRESS_REG_IDLE_WDT,
	.csr_in		= IPU7_BUTTRESS_REG_CSE2IUCSR,
	.csr_out	= IPU7_BUTTRESS_REG_IU2CSECSR,
	.db0_in		= IPU7_BUTTRESS_REG_CSE2IUDB0,
	.db0_out	= IPU7_BUTTRESS_REG_IU2CSEDB0,
	.data0_in	= IPU7_BUTTRESS_REG_CSE2IUDATA0,
	.data0_out	= IPU7_BUTTRESS_REG_IU2CSEDATA0,
	.sku_id		= IPU7_BUTTRESS_REG_SKU,

	/* Bitmasks */
	.irq_is		= IPU7_BUTTRESS_IRQ_IS_IRQ,
	.irq_ps		= IPU7_BUTTRESS_IRQ_PS_IRQ,
	.irq_all	= IPU7_BUTTRESS_IRQS,
	.irq_events	= IPU7_BUTTRESS_IRQS,
	.irq_cse_ipc	= IPU7_BUTTRESS_IRQ_IPC_FROM_CSE_IS_WAITING,
	.irq_exec_done	= IPU7_BUTTRESS_IRQ_IPC_EXEC_DONE_BY_CSE,
	.irq_sai	= IPU7_BUTTRESS_IRQ_SAI_VIOLATION,
};

static void
ipu6_pkg_dir_configure_spc(struct ipu6_device *isp,
			   const struct ipu6_hw_variants *hw_variant,
			   int pkg_dir_idx, void __iomem *base,
			   u64 *pkg_dir, dma_addr_t pkg_dir_vied_address)
{
	struct ipu6_cell_program *prog;
	void __iomem *spc_base;
	u32 server_fw_addr;
	dma_addr_t dma_addr;
	u32 pg_offset;

	server_fw_addr = lower_32_bits(*(pkg_dir + (pkg_dir_idx + 1) * 2));
	if (pkg_dir_idx == IPU6_CPD_PKG_DIR_ISYS_SERVER_IDX)
		dma_addr = sg_dma_address(isp->isys->fw_sgt.sgl);
	else
		dma_addr = sg_dma_address(isp->psys->fw_sgt.sgl);

	pg_offset = server_fw_addr - dma_addr;
	prog = (struct ipu6_cell_program *)((uintptr_t)isp->cpd_fw->data +
					    pg_offset);
	spc_base = base + prog->regs_addr;
	if (spc_base != (base + hw_variant->spc_offset))
		dev_warn(&isp->pdev->dev,
			 "SPC reg addr %p not matching value from CPD %p\n",
			 base + hw_variant->spc_offset, spc_base);
	writel(server_fw_addr + prog->blob_offset +
	       prog->icache_source, spc_base + IPU6_PSYS_REG_SPC_ICACHE_BASE);
	writel(IPU6_INFO_REQUEST_DESTINATION_IOSF,
	       spc_base + IPU6_REG_PSYS_INFO_SEG_0_CONFIG_ICACHE_MASTER);
	writel(prog->start[1], spc_base + IPU6_PSYS_REG_SPC_START_PC);
	writel(pkg_dir_vied_address, base + hw_variant->dmem_offset);
}

void ipu6_configure_spc(struct ipu6_device *isp,
			const struct ipu6_hw_variants *hw_variant,
			int pkg_dir_idx, void __iomem *base, u64 *pkg_dir,
			dma_addr_t pkg_dir_dma_addr)
{
	void __iomem *dmem_base;
	void __iomem *spc_regs_base;
	u32 val;

	if (IS_IPU7(isp) || IS_IPU8(isp))
		return;

	dmem_base = base + hw_variant->dmem_offset;
	spc_regs_base = base + hw_variant->spc_offset;

	val = readl(spc_regs_base + IPU6_PSYS_REG_SPC_STATUS_CTRL);
	val |= IPU6_PSYS_SPC_STATUS_CTRL_ICACHE_INVALIDATE;
	writel(val, spc_regs_base + IPU6_PSYS_REG_SPC_STATUS_CTRL);

	if (isp->secure_mode)
		writel(IPU6_PKG_DIR_IMR_OFFSET, dmem_base);
	else
		ipu6_pkg_dir_configure_spc(isp, hw_variant, pkg_dir_idx, base,
					   pkg_dir, pkg_dir_dma_addr);
}
EXPORT_SYMBOL_NS_GPL(ipu6_configure_spc, "INTEL_IPU6");

#define IPU6_ISYS_CSI2_NPORTS		4
#define IPU6SE_ISYS_CSI2_NPORTS		4
#define IPU6_TGL_ISYS_CSI2_NPORTS	8
#define IPU6EP_MTL_ISYS_CSI2_NPORTS	6

static void ipu6_internal_pdata_init(struct ipu6_device *isp)
{
	isys_ipdata.num_parallel_streams = IPU6_ISYS_NUM_STREAMS;
	isys_ipdata.sram_gran_shift = IPU6_SRAM_GRANULARITY_SHIFT;
	isys_ipdata.sram_gran_size = IPU6_SRAM_GRANULARITY_SIZE;
	isys_ipdata.max_sram_size = IPU6_MAX_SRAM_SIZE;
	isys_ipdata.sensor_type_start = IPU6_FW_ISYS_SENSOR_TYPE_START;
	isys_ipdata.sensor_type_end = IPU6_FW_ISYS_SENSOR_TYPE_END;
	isys_ipdata.max_streams = IPU6_ISYS_NUM_STREAMS;
	isys_ipdata.max_send_queues = IPU6_N_MAX_SEND_QUEUES;
	isys_ipdata.max_sram_blocks = IPU6_NOF_SRAM_BLOCKS_MAX;
	isys_ipdata.max_devq_size = IPU6_DEV_SEND_QUEUE_SIZE;
	isys_ipdata.csi2.nports = IPU6_ISYS_CSI2_NPORTS;
	isys_ipdata.csi2.irq_mask = IPU6_CSI_RX_ERROR_IRQ_MASK;
	isys_ipdata.csi2.ctrl0_irq_edge = IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_EDGE;
	isys_ipdata.csi2.ctrl0_irq_clear =
		IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_CLEAR;
	isys_ipdata.csi2.ctrl0_irq_mask = IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_MASK;
	isys_ipdata.csi2.ctrl0_irq_enable =
		IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_ENABLE;
	isys_ipdata.csi2.ctrl0_irq_status =
		IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_STATUS;
	isys_ipdata.csi2.ctrl0_irq_lnp =
		IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_LEVEL_NOT_PULSE;
	isys_ipdata.enhanced_iwake = IS_IPU6EP_MTL(isp) || IS_IPU6EP(isp);
	psys_ipdata.hw_variant.spc_offset = IPU6_PSYS_SPC_OFFSET;
	isys_ipdata.csi2.fw_access_port_ofs = CSI_REG_HUB_FW_ACCESS_PORT_OFS;

	if (IS_IPU6EP(isp)) {
		isys_ipdata.ltr = IPU6EP_LTR_VALUE;
		isys_ipdata.memopen_threshold = IPU6EP_MIN_MEMOPEN_TH;
	}

	if (IS_IPU6_TGL(isp))
		isys_ipdata.csi2.nports = IPU6_TGL_ISYS_CSI2_NPORTS;

	if (IS_IPU6EP_MTL(isp)) {
		isys_ipdata.csi2.nports = IPU6EP_MTL_ISYS_CSI2_NPORTS;

		isys_ipdata.csi2.ctrl0_irq_edge =
			IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_EDGE;
		isys_ipdata.csi2.ctrl0_irq_clear =
			IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_CLEAR;
		isys_ipdata.csi2.ctrl0_irq_mask =
			IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_MASK;
		isys_ipdata.csi2.ctrl0_irq_enable =
			IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_ENABLE;
		isys_ipdata.csi2.ctrl0_irq_lnp =
			IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_LEVEL_NOT_PULSE;
		isys_ipdata.csi2.ctrl0_irq_status =
			IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_STATUS;
		isys_ipdata.csi2.fw_access_port_ofs =
			CSI_REG_HUB_FW_ACCESS_PORT_V6OFS;
		isys_ipdata.ltr = IPU6EP_MTL_LTR_VALUE;
		isys_ipdata.memopen_threshold = IPU6EP_MTL_MIN_MEMOPEN_TH;
	}

	if (IS_IPU6SE(isp)) {
		isys_ipdata.csi2.nports = IPU6SE_ISYS_CSI2_NPORTS;
		isys_ipdata.csi2.irq_mask = IPU6SE_CSI_RX_ERROR_IRQ_MASK;
		isys_ipdata.num_parallel_streams = IPU6SE_ISYS_NUM_STREAMS;
		isys_ipdata.sram_gran_shift = IPU6SE_SRAM_GRANULARITY_SHIFT;
		isys_ipdata.sram_gran_size = IPU6SE_SRAM_GRANULARITY_SIZE;
		isys_ipdata.max_sram_size = IPU6SE_MAX_SRAM_SIZE;
		isys_ipdata.sensor_type_start =
			IPU6SE_FW_ISYS_SENSOR_TYPE_START;
		isys_ipdata.sensor_type_end = IPU6SE_FW_ISYS_SENSOR_TYPE_END;
		isys_ipdata.max_streams = IPU6SE_ISYS_NUM_STREAMS;
		isys_ipdata.max_send_queues = IPU6SE_N_MAX_SEND_QUEUES;
		isys_ipdata.max_sram_blocks = IPU6SE_NOF_SRAM_BLOCKS_MAX;
		isys_ipdata.max_devq_size = IPU6SE_DEV_SEND_QUEUE_SIZE;
		psys_ipdata.hw_variant.spc_offset = IPU6SE_PSYS_SPC_OFFSET;
	}

	if (IS_IPU7(isp)) {
		isys_ipdata.csi2.gpreg = IPU7_IS_IO_CSI2_GPREGS_BASE;
		isys_ipdata.csi2.gpreg_stride = IPU7_IS_IO_CSI2_GPREGS_STRIDE;
		isys_ipdata.csi2.nports = 4;
	}

	if (IS_IPU8(isp)) {
		isys_ipdata.csi2.gpreg = IPU8_IS_IO_CSI2_GPREGS_BASE;
		isys_ipdata.csi2.gpreg_stride = IPU8_IS_IO_CSI2_GPREGS_STRIDE;
		isys_ipdata.csi2.nports = 4;
	}
}

static struct ipu6_bus_device *
ipu6_isys_init(struct pci_dev *pdev, struct device *parent,
	       const struct ipu6_buttress_ctrl *ctrl, void __iomem *base,
	       const struct ipu6_isys_internal_pdata *ipdata)
{
	struct device *dev = &pdev->dev;
	struct ipu6_bus_device *isys_adev;
	struct ipu6_buttress_ctrl *devm_ctrl;
	struct ipu6_isys_pdata *pdata;
	int ret;

	ret = ipu_bridge_init(dev, ipu_bridge_parse_ssdb);
	if (ret) {
		dev_err_probe(dev, ret, "IPU6 bridge init failed\n");
		return ERR_PTR(ret);
	}

	devm_ctrl = devm_kmemdup(dev, ctrl, sizeof(*ctrl), GFP_KERNEL);
	if (!devm_ctrl)
		return ERR_PTR(-ENOMEM);

	pdata = kzalloc_obj(*pdata);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->base = base;
	pdata->ipdata = ipdata;

	isys_adev = ipu6_bus_initialize_device(pdev, parent, pdata, devm_ctrl,
					       IPU6_ISYS_NAME);
	if (IS_ERR(isys_adev)) {
		kfree(pdata);
		return dev_err_cast_probe(dev, isys_adev,
				"ipu6_bus_initialize_device isys failed\n");
	}

	isys_adev->mmu = ipu6_mmu_init(dev, base, IPU_ISYS);
	if (IS_ERR(isys_adev->mmu)) {
		put_device(&isys_adev->auxdev.dev);
		return dev_err_cast_probe(dev, isys_adev->mmu,
				"ipu6_mmu_init(isys_adev->mmu) failed\n");
	}

	isys_adev->mmu->dev = &isys_adev->auxdev.dev;

	ret = ipu6_bus_add_device(isys_adev);
	if (ret)
		return ERR_PTR(ret);

	return isys_adev;
}

static struct ipu6_bus_device *
ipu6_psys_init(struct pci_dev *pdev, struct device *parent,
	       const struct ipu6_buttress_ctrl *ctrl, void __iomem *base,
	       const struct ipu6_psys_internal_pdata *ipdata)
{
	struct device *dev = &pdev->dev;
	struct ipu6_bus_device *psys_adev;
	struct ipu6_buttress_ctrl *devm_ctrl;
	struct ipu6_psys_pdata *pdata;
	int ret;

	devm_ctrl = devm_kmemdup(dev, ctrl, sizeof(*ctrl), GFP_KERNEL);
	if (!devm_ctrl)
		return ERR_PTR(-ENOMEM);

	pdata = kzalloc_obj(*pdata);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->base = base;
	pdata->ipdata = ipdata;

	psys_adev = ipu6_bus_initialize_device(pdev, parent, pdata, devm_ctrl,
					       IPU6_PSYS_NAME);
	if (IS_ERR(psys_adev)) {
		kfree(pdata);
		return dev_err_cast_probe(&pdev->dev, psys_adev,
				"ipu6_bus_initialize_device psys failed\n");
	}

	psys_adev->mmu = ipu6_mmu_init(&pdev->dev, base, IPU_PSYS);
	if (IS_ERR(psys_adev->mmu)) {
		put_device(&psys_adev->auxdev.dev);
		return dev_err_cast_probe(&pdev->dev, psys_adev->mmu,
				"ipu6_mmu_init(psys_adev->mmu) failed\n");
	}

	psys_adev->mmu->dev = &psys_adev->auxdev.dev;

	ret = ipu6_bus_add_device(psys_adev);
	if (ret)
		return ERR_PTR(ret);

	return psys_adev;
}

static int ipu6_pci_config_setup(struct pci_dev *dev)
{
	struct ipu6_device *isp = pci_get_drvdata(dev);
	int ret;

	/* No PCI msi capability for IPU6EP */
	if (IS_IPU6EP(isp) || IS_IPU6EP_MTL(isp)) {
		/* likely do nothing as msi not enabled by default */
		pci_disable_msi(dev);
		return 0;
	}

	ret = pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_MSI);
	if (ret < 0)
		return dev_err_probe(&dev->dev, ret, "Request msi failed");

	return 0;
}

static void ipu6_configure_vc_mechanism(struct ipu6_device *isp)
{
	u32 val;

	if (IS_IPU7(isp) || IS_IPU8(isp))
		return;

	val = readl(isp->base + BUTTRESS_REG_BTRS_CTRL);

	if (IPU6_BTRS_ARB_STALL_MODE_VC0 == IPU6_BTRS_ARB_MODE_TYPE_STALL)
		val |= BUTTRESS_REG_BTRS_CTRL_STALL_MODE_VC0;
	else
		val &= ~BUTTRESS_REG_BTRS_CTRL_STALL_MODE_VC0;

	if (IPU6_BTRS_ARB_STALL_MODE_VC1 == IPU6_BTRS_ARB_MODE_TYPE_STALL)
		val |= BUTTRESS_REG_BTRS_CTRL_STALL_MODE_VC1;
	else
		val &= ~BUTTRESS_REG_BTRS_CTRL_STALL_MODE_VC1;

	writel(val, isp->base + BUTTRESS_REG_BTRS_CTRL);
}

static int __ipu6_map_fw_by_sys(struct ipu6_device *isp, struct ipu6_bus_device *adev)
{
	int ret;

	ret = ipu6_map_fw_region(adev, isp->cpd_fw->data, isp->cpd_fw->size,
				 DMA_TO_DEVICE, 0);
	if (ret) {
		dev_err_probe(&isp->pdev->dev, ret,
			      "Firmware mapping failed\n");
		return ret;
	}

	ret = ipu6_cpd_create_pkg_dir(adev, isp->cpd_fw->data);
	if (ret) {
		dev_err_probe(&isp->pdev->dev, ret,
			      "failed to create pkg dir\n");
		return ret;
	}

	return 0;
}

static int ipu6_map_fw(struct ipu6_device *isp)
{
	int ret;

	ret = __ipu6_map_fw_by_sys(isp, isp->psys);
	if (ret)
		return ret;

	if (!isp->secure_mode)
		return __ipu6_map_fw_by_sys(isp, isp->isys);

	return 0;
}

static int __ipu7_map_fw_non_secure(struct ipu6_device *isp)
{
	int ret;

	/*
	 * Allocate and map memory for running the firmware. Not
	 * required in secure mode, in which firmware runs in IMR.
	 */
	isp->fw_code_region = vmalloc(IPU7_FW_CODE_REGION_SIZE);
	if (!isp->fw_code_region)
		return -ENOMEM;

	ret = ipu6_ipu7_cpd_copy_binary(isp->cpd_fw->data, "isys",
					isp->fw_code_region,
					&isp->isys->fw_entry);
	if (ret)
		return ret;

	ret = ipu6_map_fw_region(isp->isys, isp->fw_code_region,
				 IPU7_FW_CODE_REGION_SIZE, DMA_BIDIRECTIONAL,
				 DMA_ATTR_RESERVE_REGION);
	if (ret)
		return ret;

	ret = ipu6_ipu7_cpd_copy_binary(isp->cpd_fw->data, "psys",
					isp->fw_code_region,
					&isp->psys->fw_entry);
	if (ret)
		return ret;

	return ipu6_map_fw_region(isp->psys, isp->fw_code_region,
				  IPU7_FW_CODE_REGION_SIZE, DMA_BIDIRECTIONAL,
				  DMA_ATTR_RESERVE_REGION);
}

static int ipu7_map_fw(struct ipu6_device *isp)
{
	int ret;

	ret = isp->secure_mode ?
		ipu6_map_fw_region(isp->psys, isp->cpd_fw->data,
				   isp->cpd_fw->size, DMA_BIDIRECTIONAL, 0) :
		__ipu7_map_fw_non_secure(isp);
	if (ret) {
		dev_err_probe(&isp->pdev->dev, ret,
			      "Failed to init ipu7 firmware region\n");
		return ret;
	}

	return 0;
}

static int ipu6_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	const struct ipu6_buttress_ctrl *isys_ctrl, *psys_ctrl;
	struct device *dev = &pdev->dev;
	void __iomem *isys_base = NULL;
	void __iomem *psys_base = NULL;
	struct ipu6_device *isp;
	phys_addr_t phys;
	u32 val, version, sku_id;
	unsigned long dir;
	int ret;

	if ((id->device == PCI_DEVICE_ID_INTEL_IPU7 ||
	     id->device == PCI_DEVICE_ID_INTEL_IPU7P5) && force_no_probe_ipu7)
		return -ENODEV;

	isp = devm_kzalloc(dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->cpd_metadata_cmpnt_size = sizeof(struct ipu6_cpd_metadata_cmpnt);
	isp->buttress.regs = &ipu6_buttress_regs;
	isp->model_name = IPU6_MEDIA_DEV_MODEL_NAME;
	isys_ctrl = &ipu6_isys_buttress_ctrl;
	psys_ctrl = &ipu6_psys_buttress_ctrl;

	switch (id->device) {
	case PCI_DEVICE_ID_INTEL_IPU6:
		isp->hw_ver = IPU_VERSION_6;
		isp->cpd_fw_name = IPU6_FIRMWARE_NAME;
		break;
	case PCI_DEVICE_ID_INTEL_IPU6SE:
		isp->hw_ver = IPU_VERSION_6SE;
		isp->cpd_fw_name = IPU6SE_FIRMWARE_NAME;
		isp->cpd_metadata_cmpnt_size =
			sizeof(struct ipu6se_cpd_metadata_cmpnt);
		break;
	case PCI_DEVICE_ID_INTEL_IPU6EP_ADLP:
	case PCI_DEVICE_ID_INTEL_IPU6EP_RPLP:
		isp->hw_ver = IPU_VERSION_6EP;
		isp->cpd_fw_name = IPU6EP_FIRMWARE_NAME;
		break;
	case PCI_DEVICE_ID_INTEL_IPU6EP_ADLN:
		isp->hw_ver = IPU_VERSION_6EP;
		isp->cpd_fw_name = IPU6EPADLN_FIRMWARE_NAME;
		break;
	case PCI_DEVICE_ID_INTEL_IPU6EP_MTL:
		isp->hw_ver = IPU_VERSION_6EP_MTL;
		isp->cpd_fw_name = IPU6EPMTL_FIRMWARE_NAME;
		break;
	case PCI_DEVICE_ID_INTEL_IPU7:
		isp->hw_ver = IPU_VERSION_7;
		isp->cpd_fw_name = IPU7_FIRMWARE_NAME;
		isp->model_name = IPU7_MEDIA_DEV_MODEL_NAME;
		isp->buttress.regs = &ipu7_buttress_regs;
		isys_ctrl = &ipu7_isys_buttress_ctrl;
		psys_ctrl = &ipu7_psys_buttress_ctrl;
		break;
	case PCI_DEVICE_ID_INTEL_IPU7P5:
		isp->hw_ver = IPU_VERSION_7P5;
		isp->cpd_fw_name = IPU7P5_FIRMWARE_NAME;
		isp->model_name = IPU7P5_MEDIA_DEV_MODEL_NAME;
		isp->buttress.regs = &ipu7p5_buttress_regs;
		isys_ctrl = &ipu7_isys_buttress_ctrl;
		psys_ctrl = &ipu7_psys_buttress_ctrl;
		break;
	case PCI_DEVICE_ID_INTEL_IPU8:
		/*
		 * IPU8 reuses the IPU7P5 buttress register map,
		 * matching staging ipu7 (no dedicated ipu8_buttress_regs
		 * there either). isys/psys control ratios use IPU8's own
		 * 0x10 default, unlike IPU7/IPU7P5.
		 * TODO: confirm buttress register map against IPU8 hw spec.
		 */
		isp->hw_ver = IPU_VERSION_8;
		isp->cpd_fw_name = IPU8_FIRMWARE_NAME;
		isp->model_name = IPU8_MEDIA_DEV_MODEL_NAME;
		isp->buttress.regs = &ipu7p5_buttress_regs;
		isys_ctrl = &ipu8_isys_buttress_ctrl;
		psys_ctrl = &ipu8_psys_buttress_ctrl;
		break;
	default:
		return dev_err_probe(dev, -ENODEV,
				     "Unsupported IPU6 device %x\n",
				     id->device);
	}

	isp->pdev = pdev;
	INIT_LIST_HEAD(&isp->devices);

	ret = pcim_enable_device(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "Enable PCI device failed\n");

	phys = pci_resource_start(pdev, IPU6_PCI_BAR);
	dev_dbg(dev, "IPU6 PCI bar[%u] = %pa\n", IPU6_PCI_BAR, &phys);

	isp->base = pcim_iomap_region(pdev, IPU6_PCI_BAR, IPU6_NAME);
	if (IS_ERR(isp->base))
		return dev_err_probe(dev, PTR_ERR(isp->base),
				     "Failed to I/O mem remapping\n");

	if (IS_IPU7(isp) || IS_IPU8(isp)) {
		isp->pb_base = pcim_iomap_region(pdev, IPU7_PCI_PBBAR,
						 IPU6_NAME);
		if (IS_ERR(isp->pb_base))
			return dev_err_probe(dev, PTR_ERR(isp->pb_base),
					     "I/O remapping PB BAR %u failed\n",
					     IPU7_PCI_PBBAR);
	}

	pci_set_drvdata(pdev, isp);
	pci_set_master(pdev);

	ipu6_internal_pdata_init(isp);

	isys_base = isp->base + isys_ipdata.hw_variant.offset;
	psys_base = isp->base + psys_ipdata.hw_variant.offset;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(39));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set DMA mask\n");

	dma_set_max_seg_size(dev, UINT_MAX);

	ret = ipu6_pci_config_setup(pdev);
	if (ret)
		return ret;

	ret = ipu6_buttress_init(isp);
	if (ret)
		return ret;

	ret = request_firmware(&isp->cpd_fw, isp->cpd_fw_name, dev);
	if (ret) {
		dev_err_probe(&isp->pdev->dev, ret,
			      "Requesting signed firmware %s failed\n",
			      isp->cpd_fw_name);
		goto buttress_exit;
	}

	ret = ipu6_cpd_validate_cpd_file(isp, isp->cpd_fw->data,
					 isp->cpd_fw->size);
	if (ret) {
		dev_err_probe(&isp->pdev->dev, ret,
			      "Failed to validate cpd\n");
		goto out_ipu6_bus_del_devices;
	}

	isp->isys = ipu6_isys_init(pdev, dev, isys_ctrl, isys_base,
				   &isys_ipdata);
	if (IS_ERR(isp->isys)) {
		ret = PTR_ERR(isp->isys);
		goto out_ipu6_bus_del_devices;
	}

	isp->psys = ipu6_psys_init(pdev, &isp->isys->auxdev.dev, psys_ctrl,
				   psys_base, &psys_ipdata);
	if (IS_ERR(isp->psys)) {
		ret = PTR_ERR(isp->psys);
		goto out_ipu6_bus_del_devices;
	}

	ret = pm_runtime_resume_and_get(&isp->psys->auxdev.dev);
	if (ret < 0)
		goto out_ipu6_bus_del_devices;

	ret = ipu6_mmu_hw_init(isp->psys->mmu);
	if (ret) {
		dev_err_probe(&isp->pdev->dev, ret,
			      "Failed to set MMU hardware\n");
		goto out_ipu6_rpm_put;
	}

	ret = (IS_IPU7(isp) || IS_IPU8(isp)) ? ipu7_map_fw(isp) : ipu6_map_fw(isp);
	if (ret)
		goto out_ipu6_rpm_put;

	ret = devm_request_threaded_irq(dev, pdev->irq, ipu6_buttress_isr,
					ipu6_buttress_isr_threaded,
					IRQF_SHARED, IPU6_NAME, isp);
	if (ret) {
		dev_err_probe(dev, ret, "Requesting irq failed\n");
		goto out_ipu6_rpm_put;
	}

	ret = ipu6_buttress_authenticate(isp);
	if (ret) {
		dev_err_probe(&isp->pdev->dev, ret,
			      "FW authentication failed\n");
		goto out_free_irq;
	}

	ipu6_mmu_hw_cleanup(isp->psys->mmu);
	pm_runtime_put(&isp->psys->auxdev.dev);

	/* Configure the arbitration mechanisms for VC requests */
	ipu6_configure_vc_mechanism(isp);

	val = readl(isp->base + isp->buttress.regs->sku_id);
	sku_id = FIELD_GET(GENMASK(6, 4), val);
	version = FIELD_GET(GENMASK(3, 0), val);
	dev_info(dev, "IPU%u-v%u[%x] hardware version %d\n", version, sku_id,
		 pdev->device, isp->hw_ver);

	pm_runtime_put_noidle(dev);
	pm_runtime_allow(dev);

	isp->bus_ready_to_probe = true;

	return 0;

out_free_irq:
	devm_free_irq(dev, pdev->irq, isp);
out_ipu6_rpm_put:
	pm_runtime_put_sync(&isp->psys->auxdev.dev);
out_ipu6_bus_del_devices:
	dir = (IS_IPU7(isp) || IS_IPU8(isp)) ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE;
	if (!IS_ERR_OR_NULL(isp->psys)) {
		ipu6_cpd_free_pkg_dir(isp->psys);
		if (isp->psys->fw_sgt.nents)
			ipu6_unmap_fw_region(isp->psys, dir);
	}
	if (!IS_ERR_OR_NULL(isp->isys)) {
		ipu6_cpd_free_pkg_dir(isp->isys);
		if (isp->isys->fw_sgt.nents)
			ipu6_unmap_fw_region(isp->isys, dir);
	}

	vfree(isp->fw_code_region);
	isp->fw_code_region = NULL;

	if (!IS_ERR_OR_NULL(isp->psys) && !IS_ERR_OR_NULL(isp->psys->mmu))
		ipu6_mmu_cleanup(isp->psys->mmu);
	if (!IS_ERR_OR_NULL(isp->isys) && !IS_ERR_OR_NULL(isp->isys->mmu))
		ipu6_mmu_cleanup(isp->isys->mmu);
	ipu6_bus_del_devices(pdev);
	release_firmware(isp->cpd_fw);
buttress_exit:
	ipu6_buttress_exit(isp);

	return ret;
}

static void ipu6_pci_remove(struct pci_dev *pdev)
{
	struct ipu6_device *isp = pci_get_drvdata(pdev);
	struct ipu6_mmu *isys_mmu = isp->isys->mmu;
	struct ipu6_mmu *psys_mmu = isp->psys->mmu;
	unsigned long dir;

	devm_free_irq(&pdev->dev, pdev->irq, isp);

	dir = (IS_IPU7(isp) || IS_IPU8(isp)) ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE;
	ipu6_cpd_free_pkg_dir(isp->psys);
	ipu6_unmap_fw_region(isp->psys, dir);

	ipu6_cpd_free_pkg_dir(isp->isys);
	if (isp->isys->fw_sgt.nents)
		ipu6_unmap_fw_region(isp->isys, dir);

	vfree(isp->fw_code_region);
	isp->fw_code_region = NULL;

	ipu6_buttress_exit(isp);

	ipu6_bus_del_devices(pdev);

	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);

	release_firmware(isp->cpd_fw);

	ipu6_mmu_cleanup(psys_mmu);
	ipu6_mmu_cleanup(isys_mmu);
}

static void ipu6_pci_reset_prepare(struct pci_dev *pdev)
{
	struct ipu6_device *isp = pci_get_drvdata(pdev);

	pm_runtime_forbid(&isp->pdev->dev);
}

static void ipu6_pci_reset_done(struct pci_dev *pdev)
{
	struct ipu6_device *isp = pci_get_drvdata(pdev);

	ipu6_buttress_restore(isp);
	if (isp->secure_mode)
		ipu6_buttress_reset_authentication(isp);

	isp->need_ipc_reset = true;
	pm_runtime_allow(&isp->pdev->dev);
}

/*
 * PCI base driver code requires driver to provide these to enable
 * PCI device level PM state transitions (D0<->D3)
 */
static int ipu6_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	synchronize_irq(pdev->irq);
	return 0;
}

static int ipu6_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ipu6_device *isp = pci_get_drvdata(pdev);
	int ret;

	/* Configure the arbitration mechanisms for VC requests */
	ipu6_configure_vc_mechanism(isp);

	isp->secure_mode = ipu6_buttress_get_secure_mode(isp);
	dev_info(dev, "IPU6 in %s mode\n",
		 isp->secure_mode ? "secure" : "non-secure");

	ipu6_buttress_restore(isp);

	ret = ipu6_buttress_ipc_reset(isp);
	if (ret)
		dev_err(&isp->pdev->dev, "IPC reset protocol failed!\n");

	ret = pm_runtime_resume_and_get(&isp->psys->auxdev.dev);
	if (ret < 0) {
		dev_err(&isp->psys->auxdev.dev, "Failed to get runtime PM\n");
		return 0;
	}

	ret = ipu6_buttress_authenticate(isp);
	if (ret)
		dev_err(&isp->pdev->dev, "FW authentication failed(%d)\n", ret);

	pm_runtime_put(&isp->psys->auxdev.dev);

	return 0;
}

static int ipu6_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ipu6_device *isp = pci_get_drvdata(pdev);
	int ret;

	ipu6_configure_vc_mechanism(isp);
	ipu6_buttress_restore(isp);

	if (isp->need_ipc_reset) {
		isp->need_ipc_reset = false;
		ret = ipu6_buttress_ipc_reset(isp);
		if (ret)
			dev_err(&isp->pdev->dev, "IPC reset protocol failed\n");
	}

	return 0;
}

static const struct dev_pm_ops ipu6_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(&ipu6_suspend, &ipu6_resume)
	RUNTIME_PM_OPS(&ipu6_suspend, &ipu6_runtime_resume, NULL)
};

MODULE_DEVICE_TABLE(pci, ipu6_pci_tbl);

static const struct pci_error_handlers pci_err_handlers = {
	.reset_prepare = ipu6_pci_reset_prepare,
	.reset_done = ipu6_pci_reset_done,
};

static struct pci_driver ipu6_pci_driver = {
	.name = IPU6_NAME,
	.id_table = ipu6_pci_tbl,
	.probe = ipu6_pci_probe,
	.remove = ipu6_pci_remove,
	.driver = {
		.pm = pm_ptr(&ipu6_pm_ops),
	},
	.err_handler = &pci_err_handlers,
};

module_pci_driver(ipu6_pci_driver);

MODULE_IMPORT_NS("INTEL_IPU_BRIDGE");
MODULE_AUTHOR("Sakari Ailus <sakari.ailus@linux.intel.com>");
MODULE_AUTHOR("Tianshu Qiu <tian.shu.qiu@intel.com>");
MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_AUTHOR("Qingwu Zhang <qingwu.zhang@intel.com>");
MODULE_AUTHOR("Yunliang Ding <yunliang.ding@intel.com>");
MODULE_AUTHOR("Hongju Wang");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel IPU6 PCI driver");
