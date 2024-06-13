// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 */

#include <linux/bits.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/types.h>

#include "t7xx_pci.h"
#include "t7xx_pcie_mac.h"
#include "t7xx_reg.h"

#define T7XX_PCIE_REG_BAR		2
#define T7XX_PCIE_REG_PORT		ATR_SRC_PCI_WIN0
#define T7XX_PCIE_REG_TABLE_NUM		0
#define T7XX_PCIE_REG_TRSL_PORT		ATR_DST_AXIM_0

#define T7XX_PCIE_DEV_DMA_PORT_START	ATR_SRC_AXIS_0
#define T7XX_PCIE_DEV_DMA_PORT_END	ATR_SRC_AXIS_2
#define T7XX_PCIE_DEV_DMA_TABLE_NUM	0
#define T7XX_PCIE_DEV_DMA_TRSL_ADDR	0
#define T7XX_PCIE_DEV_DMA_SRC_ADDR	0
#define T7XX_PCIE_DEV_DMA_TRANSPARENT	1
#define T7XX_PCIE_DEV_DMA_SIZE		0

enum t7xx_atr_src_port {
	ATR_SRC_PCI_WIN0,
	ATR_SRC_PCI_WIN1,
	ATR_SRC_AXIS_0,
	ATR_SRC_AXIS_1,
	ATR_SRC_AXIS_2,
	ATR_SRC_AXIS_3,
};

enum t7xx_atr_dst_port {
	ATR_DST_PCI_TRX,
	ATR_DST_PCI_CONFIG,
	ATR_DST_AXIM_0 = 4,
	ATR_DST_AXIM_1,
	ATR_DST_AXIM_2,
	ATR_DST_AXIM_3,
};

struct t7xx_atr_config {
	u64			src_addr;
	u64			trsl_addr;
	u64			size;
	u32			port;
	u32			table;
	enum t7xx_atr_dst_port	trsl_id;
	u32			transparent;
};

static void t7xx_pcie_mac_atr_tables_dis(void __iomem *pbase, enum t7xx_atr_src_port port)
{
	void __iomem *reg;
	int i, offset;

	for (i = 0; i < ATR_TABLE_NUM_PER_ATR; i++) {
		offset = ATR_PORT_OFFSET * port + ATR_TABLE_OFFSET * i;
		reg = pbase + ATR_PCIE_WIN0_T0_ATR_PARAM_SRC_ADDR + offset;
		iowrite64(0, reg);
	}
}

static int t7xx_pcie_mac_atr_cfg(struct t7xx_pci_dev *t7xx_dev, struct t7xx_atr_config *cfg)
{
	struct device *dev = &t7xx_dev->pdev->dev;
	void __iomem *pbase = IREG_BASE(t7xx_dev);
	int atr_size, pos, offset;
	void __iomem *reg;
	u64 value;

	if (cfg->transparent) {
		/* No address conversion is performed */
		atr_size = ATR_TRANSPARENT_SIZE;
	} else {
		if (cfg->src_addr & (cfg->size - 1)) {
			dev_err(dev, "Source address is not aligned to size\n");
			return -EINVAL;
		}

		if (cfg->trsl_addr & (cfg->size - 1)) {
			dev_err(dev, "Translation address %llx is not aligned to size %llx\n",
				cfg->trsl_addr, cfg->size - 1);
			return -EINVAL;
		}

		pos = __ffs64(cfg->size);

		/* HW calculates the address translation space as 2^(atr_size + 1) */
		atr_size = pos - 1;
	}

	offset = ATR_PORT_OFFSET * cfg->port + ATR_TABLE_OFFSET * cfg->table;

	reg = pbase + ATR_PCIE_WIN0_T0_TRSL_ADDR + offset;
	value = cfg->trsl_addr & ATR_PCIE_WIN0_ADDR_ALGMT;
	iowrite64(value, reg);

	reg = pbase + ATR_PCIE_WIN0_T0_TRSL_PARAM + offset;
	iowrite32(cfg->trsl_id, reg);

	reg = pbase + ATR_PCIE_WIN0_T0_ATR_PARAM_SRC_ADDR + offset;
	value = (cfg->src_addr & ATR_PCIE_WIN0_ADDR_ALGMT) | (atr_size << 1) | BIT(0);
	iowrite64(value, reg);

	/* Ensure ATR is set */
	ioread64(reg);
	return 0;
}

/**
 * t7xx_pcie_mac_atr_init() - Initialize address translation.
 * @t7xx_dev: MTK device.
 *
 * Setup ATR for ports & device.
 */
void t7xx_pcie_mac_atr_init(struct t7xx_pci_dev *t7xx_dev)
{
	struct t7xx_atr_config cfg;
	u32 i;

	/* Disable for all ports */
	for (i = ATR_SRC_PCI_WIN0; i <= ATR_SRC_AXIS_3; i++)
		t7xx_pcie_mac_atr_tables_dis(IREG_BASE(t7xx_dev), i);

	memset(&cfg, 0, sizeof(cfg));
	/* Config ATR for RC to access device's register */
	cfg.src_addr = pci_resource_start(t7xx_dev->pdev, T7XX_PCIE_REG_BAR);
	cfg.size = T7XX_PCIE_REG_SIZE_CHIP;
	cfg.trsl_addr = T7XX_PCIE_REG_TRSL_ADDR_CHIP;
	cfg.port = T7XX_PCIE_REG_PORT;
	cfg.table = T7XX_PCIE_REG_TABLE_NUM;
	cfg.trsl_id = T7XX_PCIE_REG_TRSL_PORT;
	t7xx_pcie_mac_atr_tables_dis(IREG_BASE(t7xx_dev), cfg.port);
	t7xx_pcie_mac_atr_cfg(t7xx_dev, &cfg);

	t7xx_dev->base_addr.pcie_dev_reg_trsl_addr = T7XX_PCIE_REG_TRSL_ADDR_CHIP;

	/* Config ATR for EP to access RC's memory */
	for (i = T7XX_PCIE_DEV_DMA_PORT_START; i <= T7XX_PCIE_DEV_DMA_PORT_END; i++) {
		cfg.src_addr = T7XX_PCIE_DEV_DMA_SRC_ADDR;
		cfg.size = T7XX_PCIE_DEV_DMA_SIZE;
		cfg.trsl_addr = T7XX_PCIE_DEV_DMA_TRSL_ADDR;
		cfg.port = i;
		cfg.table = T7XX_PCIE_DEV_DMA_TABLE_NUM;
		cfg.trsl_id = ATR_DST_PCI_TRX;
		cfg.transparent = T7XX_PCIE_DEV_DMA_TRANSPARENT;
		t7xx_pcie_mac_atr_tables_dis(IREG_BASE(t7xx_dev), cfg.port);
		t7xx_pcie_mac_atr_cfg(t7xx_dev, &cfg);
	}
}

/**
 * t7xx_pcie_mac_enable_disable_int() - Enable/disable interrupts.
 * @t7xx_dev: MTK device.
 * @enable: Enable/disable.
 *
 * Enable or disable device interrupts.
 */
static void t7xx_pcie_mac_enable_disable_int(struct t7xx_pci_dev *t7xx_dev, bool enable)
{
	u32 value;

	value = ioread32(IREG_BASE(t7xx_dev) + ISTAT_HST_CTRL);

	if (enable)
		value &= ~ISTAT_HST_CTRL_DIS;
	else
		value |= ISTAT_HST_CTRL_DIS;

	iowrite32(value, IREG_BASE(t7xx_dev) + ISTAT_HST_CTRL);
}

void t7xx_pcie_mac_interrupts_en(struct t7xx_pci_dev *t7xx_dev)
{
	t7xx_pcie_mac_enable_disable_int(t7xx_dev, true);
}

void t7xx_pcie_mac_interrupts_dis(struct t7xx_pci_dev *t7xx_dev)
{
	t7xx_pcie_mac_enable_disable_int(t7xx_dev, false);
}

/**
 * t7xx_pcie_mac_clear_set_int() - Clear/set interrupt by type.
 * @t7xx_dev: MTK device.
 * @int_type: Interrupt type.
 * @clear: Clear/set.
 *
 * Clear or set device interrupt by type.
 */
static void t7xx_pcie_mac_clear_set_int(struct t7xx_pci_dev *t7xx_dev,
					enum t7xx_int int_type, bool clear)
{
	void __iomem *reg;
	u32 val;

	if (clear)
		reg = IREG_BASE(t7xx_dev) + IMASK_HOST_MSIX_CLR_GRP0_0;
	else
		reg = IREG_BASE(t7xx_dev) + IMASK_HOST_MSIX_SET_GRP0_0;

	val = BIT(EXT_INT_START + int_type);
	iowrite32(val, reg);
}

void t7xx_pcie_mac_clear_int(struct t7xx_pci_dev *t7xx_dev, enum t7xx_int int_type)
{
	t7xx_pcie_mac_clear_set_int(t7xx_dev, int_type, true);
}

void t7xx_pcie_mac_set_int(struct t7xx_pci_dev *t7xx_dev, enum t7xx_int int_type)
{
	t7xx_pcie_mac_clear_set_int(t7xx_dev, int_type, false);
}

/**
 * t7xx_pcie_mac_clear_int_status() - Clear interrupt status by type.
 * @t7xx_dev: MTK device.
 * @int_type: Interrupt type.
 *
 * Enable or disable device interrupts' status by type.
 */
void t7xx_pcie_mac_clear_int_status(struct t7xx_pci_dev *t7xx_dev, enum t7xx_int int_type)
{
	void __iomem *reg = IREG_BASE(t7xx_dev) + MSIX_ISTAT_HST_GRP0_0;
	u32 val = BIT(EXT_INT_START + int_type);

	iowrite32(val, reg);
}

/**
 * t7xx_pcie_set_mac_msix_cfg() - Write MSIX control configuration.
 * @t7xx_dev: MTK device.
 * @irq_count: Number of MSIX IRQ vectors.
 *
 * Write IRQ count to device.
 */
void t7xx_pcie_set_mac_msix_cfg(struct t7xx_pci_dev *t7xx_dev, unsigned int irq_count)
{
	u32 val = ffs(irq_count) * 2 - 1;

	iowrite32(val, IREG_BASE(t7xx_dev) + T7XX_PCIE_CFG_MSIX);
}
