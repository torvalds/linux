// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/mfd/syscon.h>
#include <linux/miscdevice.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/resource.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "pcie-designware.h"
#include "rockchip-pcie-dma.h"

/* Maximum number of inbound/outbound iATUs */
#define MAX_IATU_IN			256
#define MAX_IATU_OUT			256

enum rk_pcie_device_mode {
	RK_PCIE_EP_TYPE,
	RK_PCIE_RC_TYPE,
};

enum pci_barno {
	BAR_0,
	BAR_1,
	BAR_2,
	BAR_3,
	BAR_4,
	BAR_5,
};

enum rk_pcie_as_type {
	RK_PCIE_AS_UNKNOWN,
	RK_PCIE_AS_MEM,
	RK_PCIE_AS_IO,
};

struct reset_bulk_data	{
	const char *id;
	struct reset_control *rst;
};

#define PCIE_DMA_OFFSET			0x380000
#define PCIE_DMA_WR_ENB			0xc
#define PCIE_DMA_CTRL_LO		0x200
#define PCIE_DMA_CTRL_HI		0x204
#define PCIE_DMA_XFERSIZE		0x208
#define PCIE_DMA_SAR_PTR_LO		0x20c
#define PCIE_DMA_SAR_PTR_HI		0x210
#define PCIE_DMA_DAR_PTR_LO		0x214
#define PCIE_DMA_DAR_PTR_HI		0x218
#define PCIE_DMA_WR_WEILO		0x18
#define PCIE_DMA_WR_WEIHI		0x1c
#define PCIE_DMA_WR_DOORBELL		0x10
#define PCIE_DMA_WR_INT_STATUS		0x4c
#define PCIE_DMA_WR_INT_MASK		0x54
#define PCIE_DMA_WR_INT_CLEAR		0x58
#define PCIE_DMA_RD_INT_STATUS		0xa0
#define PCIE_DMA_RD_INT_MASK		0xa8
#define PCIE_DMA_RD_INT_CLEAR		0xac

/* Parameters for the waiting for iATU enabled routine */
#define LINK_WAIT_MAX_IATU_RETRIES	5
#define LINK_WAIT_IATU_MIN		9000
#define LINK_WAIT_IATU_MAX		10000

#define PCIE_DIRECT_SPEED_CHANGE	(0x1 << 17)

#define PCIE_ATU_TYPE_MEM		(0x0 << 0)
#define PCIE_ATU_TYPE_IO		(0x2 << 0)
#define PCIE_ATU_ENABLE			(0x1 << 31)
#define PCIE_ATU_BAR_MODE_ENABLE	(0x1 << 30)

#define PCI_BASE_ADDRESS_0		0x10

#define PCIE_TYPE0_STATUS_COMMAND_REG	0x4
#define PCIE_TYPE0_BAR0_REG		0x10

#define PCIE_CAP_LINK_CONTROL2_LINK_STATUS	0xa0

#define PCIE_CLIENT_INTR_MASK		0x24
#define PCIE_CLIENT_GENERAL_DEBUG	0x104

#define PCIE_PHY_LINKUP			BIT(0)
#define PCIE_DATA_LINKUP		BIT(1)

#define PCIE_MISC_CONTROL_1_OFF		0x8BC
#define PCIE_DBI_RO_WR_EN		(0x1 << 0)

#define PCIE_RESBAR_CTRL_REG0_REG	0x2a8
#define PCIE_SB_BAR0_MASK_REG		0x100010

struct rk_pcie {
	struct device			*dev;
	enum rk_pcie_device_mode	mode;
	enum phy_mode			phy_mode;
	unsigned char			bar_to_atu[6];
	phys_addr_t			*outbound_addr;
	unsigned long			*ib_window_map;
	unsigned long			*ob_window_map;
	unsigned int			num_ib_windows;
	unsigned int			num_ob_windows;
	void __iomem			*dbi_base;
	void __iomem			*apb_base;
	struct phy			*phy;
	struct clk_bulk_data		*clks;
	unsigned int			clk_cnt;
	struct reset_bulk_data		*rsts;
	struct gpio_desc		*rst_gpio;
	phys_addr_t			mem_start;
	size_t				mem_size;
	struct pcie_port		pp;
	struct regmap			*usb_pcie_grf;
	struct regmap			*pmu_grf;
	struct dma_trx_obj		*dma_obj;
};

struct rk_pcie_of_data {
	enum rk_pcie_device_mode	mode;
};

#define to_rk_pcie(x)	container_of(x, struct rk_pcie, pp)

static int rk_pcie_read(void __iomem *addr, int size, u32 *val)
{
	if ((uintptr_t)addr & (size - 1)) {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	if (size == 4) {
		*val = readl(addr);
	} else if (size == 2) {
		*val = readw(addr);
	} else if (size == 1) {
		*val = readb(addr);
	} else {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int rk_pcie_write(void __iomem *addr, int size, u32 val)
{
	if ((uintptr_t)addr & (size - 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (size == 4)
		writel(val, addr);
	else if (size == 2)
		writew(val, addr);
	else if (size == 1)
		writeb(val, addr);
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

static u32 __rk_pcie_read_dbi(struct rk_pcie *rk_pcie, void __iomem *base,
			u32 reg, size_t size)
{
	int ret;
	u32 val;

	ret = rk_pcie_read(base + reg, size, &val);
	if (ret)
		dev_err(rk_pcie->dev, "Read DBI address failed\n");

	return val;
}

static void __rk_pcie_write_dbi(struct rk_pcie *rk_pcie, void __iomem *base,
			u32 reg, size_t size, u32 val)
{
	int ret;

	ret = rk_pcie_write(base + reg, size, val);
	if (ret)
		dev_err(rk_pcie->dev, "Write DBI address failed\n");
}

static u32 __rk_pcie_read_apb(struct rk_pcie *rk_pcie, void __iomem *base,
			u32 reg, size_t size)
{
	int ret;
	u32 val;

	ret = rk_pcie_read(base + reg, size, &val);
	if (ret)
		dev_err(rk_pcie->dev, "Read APB address failed\n");

	return val;
}

static void __rk_pcie_write_apb(struct rk_pcie *rk_pcie, void __iomem *base,
			u32 reg, size_t size, u32 val)
{
	int ret;

	ret = rk_pcie_write(base + reg, size, val);
	if (ret)
		dev_err(rk_pcie->dev, "Write APB address failed\n");
}

static inline void rk_pcie_writel_dbi(struct rk_pcie *rk_pcie, u32 reg,
					u32 val)
{
	__rk_pcie_write_dbi(rk_pcie, rk_pcie->dbi_base, reg, 0x4, val);
}

static inline u32 rk_pcie_readl_dbi(struct rk_pcie *rk_pcie, u32 reg)
{
	return __rk_pcie_read_dbi(rk_pcie, rk_pcie->dbi_base, reg, 0x4);
}

static inline u32 rk_pcie_readl_apb(struct rk_pcie *rk_pcie, u32 reg)
{
	return __rk_pcie_read_apb(rk_pcie, rk_pcie->apb_base, reg, 0x4);
}

static inline void rk_pcie_writel_apb(struct rk_pcie *rk_pcie, u32 reg,
					u32 val)
{
	__rk_pcie_write_apb(rk_pcie, rk_pcie->apb_base, reg, 0x4, val);
}

static inline void rk_pcie_dbi_ro_wr_en(struct rk_pcie *rk_pcie)
{
	u32 reg;
	u32 val;

	reg = PCIE_MISC_CONTROL_1_OFF;
	val = rk_pcie_readl_dbi(rk_pcie, reg);
	val |= PCIE_DBI_RO_WR_EN;
	rk_pcie_writel_dbi(rk_pcie, reg, val);
}

static inline void rk_pcie_dbi_ro_wr_dis(struct rk_pcie *rk_pcie)
{
	u32 reg;
	u32 val;

	reg = PCIE_MISC_CONTROL_1_OFF;
	val = rk_pcie_readl_dbi(rk_pcie, reg);
	val &= ~PCIE_DBI_RO_WR_EN;
	rk_pcie_writel_dbi(rk_pcie, reg, val);
}

static u32 rk_pcie_readl_ib_unroll(struct rk_pcie *rk_pcie, u32 index,
					     u32 reg)
{
	u32 offset = PCIE_GET_ATU_INB_UNR_REG_OFFSET(index);

	return rk_pcie_readl_dbi(rk_pcie, offset + reg);
}

static void rk_pcie_writel_ib_unroll(struct rk_pcie *rk_pcie,
						u32 index, u32 reg, u32 val)
{
	u32 offset = PCIE_GET_ATU_INB_UNR_REG_OFFSET(index);

	rk_pcie_writel_dbi(rk_pcie, offset + reg, val);
}

static u32 rk_pcie_readl_ob_unroll(struct rk_pcie *rk_pcie,
					u32 index, u32 reg)
{
	u32 offset = PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(index);

	return rk_pcie_readl_dbi(rk_pcie, offset + reg);
}

static void rk_pcie_writel_ob_unroll(struct rk_pcie *rk_pcie,
					u32 index, u32 reg, u32 val)
{
	u32 offset = PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(index);

	rk_pcie_writel_dbi(rk_pcie, offset + reg, val);
}

static int rk_pcie_prog_inbound_atu_unroll(struct rk_pcie *rk_pcie,
						int index, int bar,
						u64 cpu_addr,
						enum rk_pcie_as_type as_type)
{
	int type;
	u32 retries, val;

	rk_pcie_writel_ib_unroll(rk_pcie, index, PCIE_ATU_UNR_LOWER_TARGET,
				 lower_32_bits(cpu_addr));
	rk_pcie_writel_ib_unroll(rk_pcie, index, PCIE_ATU_UNR_UPPER_TARGET,
				 upper_32_bits(cpu_addr));

	switch (as_type) {
	case RK_PCIE_AS_MEM:
		type = PCIE_ATU_TYPE_MEM;
		break;
	case RK_PCIE_AS_IO:
		type = PCIE_ATU_TYPE_IO;
		break;
	default:
		return -EINVAL;
	}

	rk_pcie_writel_ib_unroll(rk_pcie, index, PCIE_ATU_UNR_REGION_CTRL1,
				 type);
	rk_pcie_writel_ib_unroll(rk_pcie, index, PCIE_ATU_UNR_REGION_CTRL2,
				 PCIE_ATU_ENABLE | PCIE_ATU_BAR_MODE_ENABLE |
				 (bar << 8));

	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = rk_pcie_readl_ib_unroll(rk_pcie, index,
					      PCIE_ATU_UNR_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		usleep_range(LINK_WAIT_IATU_MIN, LINK_WAIT_IATU_MAX);
	}
	dev_err(rk_pcie->dev, "Inbound iATU is not being enabled\n");

	return -EBUSY;
}

static void rk_pcie_prog_outbound_atu_unroll(struct rk_pcie *rk_pcie,
					int index, int type, u64 cpu_addr,
					u64 pci_addr, u32 size)
{
	u32 retries, val;

	rk_pcie_writel_ob_unroll(rk_pcie, index, PCIE_ATU_UNR_LOWER_BASE,
				 lower_32_bits(cpu_addr));
	rk_pcie_writel_ob_unroll(rk_pcie, index, PCIE_ATU_UNR_UPPER_BASE,
				 upper_32_bits(cpu_addr));
	rk_pcie_writel_ob_unroll(rk_pcie, index, PCIE_ATU_UNR_LIMIT,
				 lower_32_bits(cpu_addr + size - 1));
	rk_pcie_writel_ob_unroll(rk_pcie, index, PCIE_ATU_UNR_LOWER_TARGET,
				 lower_32_bits(pci_addr));
	rk_pcie_writel_ob_unroll(rk_pcie, index, PCIE_ATU_UNR_UPPER_TARGET,
				 upper_32_bits(pci_addr));
	rk_pcie_writel_ob_unroll(rk_pcie, index, PCIE_ATU_UNR_REGION_CTRL1,
				 type);
	rk_pcie_writel_ob_unroll(rk_pcie, index, PCIE_ATU_UNR_REGION_CTRL2,
				 PCIE_ATU_ENABLE);

	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = rk_pcie_readl_ob_unroll(rk_pcie, index,
					      PCIE_ATU_UNR_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return;

		usleep_range(LINK_WAIT_IATU_MIN, LINK_WAIT_IATU_MAX);
	}
	dev_err(rk_pcie->dev, "Outbound iATU is not being enabled\n");
}

static int rk_pcie_prog_inbound_atu(struct rk_pcie *rk_pcie, int index,
			int bar, u64 cpu_addr, enum rk_pcie_as_type as_type)
{
	return rk_pcie_prog_inbound_atu_unroll(rk_pcie, index, bar,
						cpu_addr, as_type);
}

static void rk_pcie_prog_outbound_atu(struct rk_pcie *rk_pcie, int index,
			int type, u64 cpu_addr, u64 pci_addr, u32 size)
{
	rk_pcie_prog_outbound_atu_unroll(rk_pcie, index, type, cpu_addr,
					pci_addr, size);
}

static int rk_pcie_ep_inbound_atu(struct rk_pcie *rk_pcie,
				enum pci_barno bar, dma_addr_t cpu_addr,
				enum rk_pcie_as_type as_type)
{
	int ret;
	u32 free_win;

	free_win = find_first_zero_bit(rk_pcie->ib_window_map,
				       rk_pcie->num_ib_windows);
	if (free_win >= rk_pcie->num_ib_windows) {
		dev_err(rk_pcie->dev, "No free inbound window\n");
		return -EINVAL;
	}

	ret = rk_pcie_prog_inbound_atu(rk_pcie, free_win, bar, cpu_addr,
				       as_type);
	if (ret < 0) {
		dev_err(rk_pcie->dev, "Failed to program IB window\n");
		return ret;
	}
	rk_pcie->bar_to_atu[bar] = free_win;
	set_bit(free_win, rk_pcie->ib_window_map);

	return 0;
}

static int rk_pcie_ep_outbound_atu(struct rk_pcie *rk_pcie,
					phys_addr_t phys_addr, u64 pci_addr,
					size_t size)
{
	u32 free_win;

	free_win = find_first_zero_bit(rk_pcie->ob_window_map,
				       rk_pcie->num_ob_windows);
	if (free_win >= rk_pcie->num_ob_windows) {
		dev_err(rk_pcie->dev, "No free outbound window\n");
		return -EINVAL;
	}

	rk_pcie_prog_outbound_atu(rk_pcie, free_win, PCIE_ATU_TYPE_MEM,
				  phys_addr, pci_addr, size);

	set_bit(free_win, rk_pcie->ob_window_map);
	rk_pcie->outbound_addr[free_win] = phys_addr;

	return 0;
}

static void __rk_pcie_ep_reset_bar(struct rk_pcie *rk_pcie,
					     enum pci_barno bar, int flags)
{
	u32 reg;

	reg = PCI_BASE_ADDRESS_0 + (4 * bar);
	rk_pcie_writel_dbi(rk_pcie, reg, 0x0);
	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64)
		rk_pcie_writel_dbi(rk_pcie, reg + 4, 0x0);
}

static void rk_pcie_ep_reset_bar(struct rk_pcie *rk_pcie, enum pci_barno bar)
{
	__rk_pcie_ep_reset_bar(rk_pcie, bar, 0);
}

static int rk_pcie_ep_atu_init(struct rk_pcie *rk_pcie,
			struct platform_device *pdev)
{
	int ret;
	enum pci_barno bar;
	enum rk_pcie_as_type as_type;
	dma_addr_t cpu_addr;
	phys_addr_t phys_addr;
	u64 pci_addr;
	size_t size;

	for (bar = BAR_0; bar <= BAR_5; bar++)
		rk_pcie_ep_reset_bar(rk_pcie, bar);

	cpu_addr = rk_pcie->mem_start;
	as_type = RK_PCIE_AS_MEM;
	ret = rk_pcie_ep_inbound_atu(rk_pcie, BAR_0, cpu_addr, as_type);
	if (ret)
		return ret;

	phys_addr = 0x0;
	pci_addr = 0x0;
	size = SZ_2G;
	ret = rk_pcie_ep_outbound_atu(rk_pcie, phys_addr, pci_addr, size);
	if (ret)
		return ret;

	return 0;
}

static int rk_pcie_link_up(struct rk_pcie *rk_pcie)
{
	u32 val = rk_pcie_readl_apb(rk_pcie, PCIE_CLIENT_GENERAL_DEBUG);

	if ((val & (PCIE_PHY_LINKUP | PCIE_DATA_LINKUP)) == 0x3)
		return 1;

	return 0;
}

static inline void rk_pcie_set_mode(struct rk_pcie *rk_pcie)
{
	switch (rk_pcie->mode) {
	case RK_PCIE_EP_TYPE:
		rk_pcie_writel_apb(rk_pcie, 0x0, 0xf00000);
		break;
	case RK_PCIE_RC_TYPE:
		rk_pcie_writel_apb(rk_pcie, 0x0, 0xf00040);
		break;
	}
}

static inline void rk_pcie_enable_ltssm(struct rk_pcie *rk_pcie)
{
	rk_pcie_writel_apb(rk_pcie, 0x0, 0xC000C);
}

static inline void rk_pcie_set_gens(struct rk_pcie *rk_pcie)
{
	rk_pcie_writel_dbi(rk_pcie, PCIE_CAP_LINK_CONTROL2_LINK_STATUS, 0x2);
}

static void rk_pcie_ep_setup(struct rk_pcie *rk_pcie)
{
	int ret;
	u32 val;
	u32 lanes;
	struct device *dev = rk_pcie->dev;
	struct device_node *np = dev->of_node;

	/* Enable client write and read interrupt */
	rk_pcie_writel_apb(rk_pcie, PCIE_CLIENT_INTR_MASK, 0xc000000);

	/* Enable core write interrupt */
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_MASK,
			   0x0);
	/* Enable core read interrupt */
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_MASK,
			   0x0);

	ret = of_property_read_u32(np, "num-lanes", &lanes);
	if (ret)
		lanes = 0;

	/* Set the number of lanes */
	val = rk_pcie_readl_dbi(rk_pcie, PCIE_PORT_LINK_CONTROL);
	val &= ~PORT_LINK_MODE_MASK;
	switch (lanes) {
	case 1:
		val |= PORT_LINK_MODE_1_LANES;
		break;
	case 2:
		val |= PORT_LINK_MODE_2_LANES;
		break;
	case 4:
		val |= PORT_LINK_MODE_4_LANES;
		break;
	case 8:
		val |= PORT_LINK_MODE_8_LANES;
		break;
	default:
		dev_err(rk_pcie->dev, "num-lanes %u: invalid value\n", lanes);
		return;
	}

	rk_pcie_writel_dbi(rk_pcie, PCIE_PORT_LINK_CONTROL, val);

	/* Set link width speed control register */
	val = rk_pcie_readl_dbi(rk_pcie, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val &= ~PORT_LOGIC_LINK_WIDTH_MASK;
	switch (lanes) {
	case 1:
		val |= PORT_LOGIC_LINK_WIDTH_1_LANES;
		break;
	case 2:
		val |= PORT_LOGIC_LINK_WIDTH_2_LANES;
		break;
	case 4:
		val |= PORT_LOGIC_LINK_WIDTH_4_LANES;
		break;
	case 8:
		val |= PORT_LOGIC_LINK_WIDTH_8_LANES;
		break;
	}

	val |= PCIE_DIRECT_SPEED_CHANGE;

	rk_pcie_writel_dbi(rk_pcie, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	/* Enable bus master and memory space */
	rk_pcie_writel_dbi(rk_pcie, PCIE_TYPE0_STATUS_COMMAND_REG, 0x6);

	/* Resize BAR0 to 4GB */
	/* bit13-8 set to 6 means 64MB */
	rk_pcie_writel_dbi(rk_pcie, PCIE_RESBAR_CTRL_REG0_REG, 0x600);

	/* Set shadow BAR0 according 64MB */
	val = rk_pcie->mem_size - 1;
	rk_pcie_writel_dbi(rk_pcie, PCIE_SB_BAR0_MASK_REG, val);

	/* Set reserved memory address to BAR0 */
	rk_pcie_writel_dbi(rk_pcie, PCIE_TYPE0_BAR0_REG,
			   rk_pcie->mem_start);
}

static int rk_pcie_establish_link(struct rk_pcie *rk_pcie)
{
	int retries;

	/* Rest the device */
	gpiod_set_value_cansleep(rk_pcie->rst_gpio, 0);
	msleep(100);
	gpiod_set_value_cansleep(rk_pcie->rst_gpio, 1);

	/* Enable LTSSM */
	rk_pcie_enable_ltssm(rk_pcie);

	for (retries = 0; retries < 1000; retries++) {
		if (rk_pcie_link_up(rk_pcie)) {
			dev_info(rk_pcie->dev, "PCIe Link up\n");
			return 0;
		}

		dev_info(rk_pcie->dev, "PCIe Linking...\n");
		mdelay(1000);
	}

	dev_err(rk_pcie->dev, "PCIe Link Fail\n");

	return -EINVAL;
}

static int rk_pcie_ep_win_parse(struct rk_pcie *rk_pcie,
				  struct platform_device *pdev)
{
	int ret;
	void *addr;
	struct device *dev = rk_pcie->dev;
	struct device_node *np = dev->of_node;

	ret = of_property_read_u32(np, "num-ib-windows",
				   &rk_pcie->num_ib_windows);
	if (ret < 0) {
		dev_err(dev, "unable to read *num-ib-windows* property\n");
		return ret;
	}

	if (rk_pcie->num_ib_windows > MAX_IATU_IN) {
		dev_err(dev, "Invalid *num-ib-windows*\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "num-ob-windows",
				   &rk_pcie->num_ob_windows);
	if (ret < 0) {
		dev_err(dev, "Unable to read *num-ob-windows* property\n");
		return ret;
	}

	if (rk_pcie->num_ob_windows > MAX_IATU_OUT) {
		dev_err(dev, "Invalid *num-ob-windows*\n");
		return -EINVAL;
	}

	rk_pcie->ib_window_map = devm_kcalloc(dev,
					BITS_TO_LONGS(rk_pcie->num_ib_windows),
					sizeof(long), GFP_KERNEL);
	if (!rk_pcie->ib_window_map)
		return -ENOMEM;

	rk_pcie->ob_window_map = devm_kcalloc(dev,
					BITS_TO_LONGS(rk_pcie->num_ob_windows),
					sizeof(long), GFP_KERNEL);
	if (!rk_pcie->ob_window_map)
		return -ENOMEM;

	addr = devm_kcalloc(dev, rk_pcie->num_ob_windows, sizeof(phys_addr_t),
			    GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	rk_pcie->outbound_addr = addr;

	return 0;
}

static int rk_pcie_msi_host_init(struct pcie_port *pp,
					struct msi_controller *chip)
{
	return 0;
}

static int rk_pcie_host_link_up(struct pcie_port *pp)
{
	struct rk_pcie *rk_pcie = to_rk_pcie(pp);

	return rk_pcie_link_up(rk_pcie);
}

static void rk_pcie_host_init(struct pcie_port *pp)
{
	dw_pcie_setup_rc(pp);
}

static struct pcie_host_ops rk_pcie_host_ops = {
	.msi_host_init = &rk_pcie_msi_host_init,
	.link_up = rk_pcie_host_link_up,
	.host_init = rk_pcie_host_init,
};

static int rk_pcie_add_host(struct rk_pcie *rk_pcie,
			       struct platform_device *pdev)
{
	int ret;
	struct pcie_port *pp = &rk_pcie->pp;

	pp->dev = &pdev->dev;
	pp->root_bus_nr = -1;
	pp->ops = &rk_pcie_host_ops;
	pp->dbi_base = rk_pcie->dbi_base;

	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static int rk_pcie_add_ep(struct rk_pcie *rk_pcie,
			       struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *mem;
	struct resource reg;

	mem = of_parse_phandle(np, "memory-region", 0);
	if (!mem) {
		dev_err(rk_pcie->dev, "missing \"memory-region\" property\n");
		return -ENODEV;
	}

	ret = of_address_to_resource(mem, 0, &reg);
	if (ret < 0) {
		dev_err(rk_pcie->dev, "missing \"reg\" property\n");
		return ret;
	}

	rk_pcie->mem_start = reg.start;
	rk_pcie->mem_size = resource_size(&reg);

	ret = rk_pcie_ep_win_parse(rk_pcie, pdev);
	if (ret) {
		dev_err(dev, "failed to parse ep dts\n");
		return ret;
	}

	ret = rk_pcie_ep_atu_init(rk_pcie, pdev);
	if (ret) {
		dev_err(dev, "failed to init ep device\n");
		return ret;
	}

	rk_pcie_ep_setup(rk_pcie);

	rk_pcie->dma_obj = rk_pcie_dma_obj_probe(dev);
		if (IS_ERR(rk_pcie->dma_obj)) {
			dev_err(dev, "failed to prepare dma object\n");
			return -EINVAL;
		}

	return 0;
}

static void rk_pcie_clk_deinit(struct rk_pcie *rk_pcie)
{
	clk_bulk_disable(rk_pcie->clk_cnt, rk_pcie->clks);
	clk_bulk_unprepare(rk_pcie->clk_cnt, rk_pcie->clks);
}

static int rk_pcie_clk_init(struct rk_pcie *rk_pcie)
{
	struct device *dev = rk_pcie->dev;
	struct property *prop;
	const char *name;
	int i = 0, ret, count;

	count = of_property_count_strings(dev->of_node, "clock-names");
	if (count < 1)
		return -ENODEV;

	rk_pcie->clks = devm_kcalloc(dev, count,
				     sizeof(struct clk_bulk_data),
				     GFP_KERNEL);
	if (!rk_pcie->clks)
		return -ENOMEM;

	rk_pcie->clk_cnt = count;

	of_property_for_each_string(dev->of_node, "clock-names", prop, name) {
		rk_pcie->clks[i].id = name;
		if (!rk_pcie->clks[i].id)
			return -ENOMEM;
		i++;
	}

	ret = devm_clk_bulk_get(dev, count, rk_pcie->clks);
	if (ret)
		return ret;

	ret = clk_bulk_prepare(count, rk_pcie->clks);
	if (ret)
		return ret;

	ret = clk_bulk_enable(count, rk_pcie->clks);
	if (ret) {
		clk_bulk_unprepare(count, rk_pcie->clks);
		return ret;
	}

	return 0;
}

static int rk_pcie_resource_get(struct platform_device *pdev,
					 struct rk_pcie *rk_pcie)
{
	struct resource *dbi_base;
	struct resource *apb_base;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"pcie-dbi");
	if (!dbi_base) {
		dev_err(&pdev->dev, "get pcie-dbi failed\n");
		return -ENODEV;
	}

	rk_pcie->dbi_base = devm_ioremap_resource(&pdev->dev, dbi_base);
	if (IS_ERR(rk_pcie->dbi_base))
		return PTR_ERR(rk_pcie->dbi_base);

	apb_base = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"pcie-apb");
	if (!apb_base) {
		dev_err(&pdev->dev, "get pcie-apb failed\n");
		return -ENODEV;
	}
	rk_pcie->apb_base = devm_ioremap_resource(&pdev->dev, apb_base);
	if (IS_ERR(rk_pcie->apb_base))
		return PTR_ERR(rk_pcie->apb_base);

	rk_pcie->rst_gpio = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(rk_pcie->rst_gpio)) {
		dev_err(&pdev->dev, "missing reset-gpios property in node\n");
		return PTR_ERR(rk_pcie->rst_gpio);
	}

	return 0;
}

static int rk_pcie_phy_init(struct rk_pcie *rk_pcie)
{
	int ret;
	struct device *dev = rk_pcie->dev;

	rk_pcie->phy = devm_phy_get(rk_pcie->dev, "pcie-phy");
	if (IS_ERR(rk_pcie->phy)) {
		if (PTR_ERR(rk_pcie->phy) != -EPROBE_DEFER)
			dev_info(rk_pcie->dev, "missing phy\n");
		return PTR_ERR(rk_pcie->phy);
	}

	switch (rk_pcie->mode) {
	case RK_PCIE_RC_TYPE:
		rk_pcie->phy_mode = PHY_MODE_PCIE_RC;
		break;
	case RK_PCIE_EP_TYPE:
		rk_pcie->phy_mode = PHY_MODE_PCIE_EP;
		break;
	}

	ret = phy_set_mode(rk_pcie->phy, rk_pcie->phy_mode);
	if (ret) {
		dev_err(dev, "fail to set phy to  mode %s, err %d\n",
			(rk_pcie->phy_mode == PHY_MODE_PCIE_RC) ? "RC" : "EP",
			ret);
		return ret;
	}

	ret = phy_init(rk_pcie->phy);
	if (ret < 0) {
		dev_err(dev, "fail to init phy, err %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk_pcie_reset_control_release(struct rk_pcie *rk_pcie)
{
	struct device *dev = rk_pcie->dev;
	struct property *prop;
	const char *name;
	int ret, count, i = 0;

	count = of_property_count_strings(dev->of_node, "reset-names");
	if (count < 1)
		return -ENODEV;

	rk_pcie->rsts = devm_kcalloc(dev, count,
				     sizeof(struct reset_bulk_data),
				     GFP_KERNEL);
	if (!rk_pcie->rsts)
		return -ENOMEM;

	of_property_for_each_string(dev->of_node, "reset-names",
				    prop, name) {
		rk_pcie->rsts[i].id = name;
		if (!rk_pcie->rsts[i].id)
			return -ENOMEM;
		i++;
	}

	for (i = 0; i < count; i++) {
		rk_pcie->rsts[i].rst = devm_reset_control_get(rk_pcie->dev,
						rk_pcie->rsts[i].id);
		if (IS_ERR(rk_pcie->rsts[i].rst)) {
			dev_err(dev, "failed to get %s\n",
				rk_pcie->clks[i].id);
			return -PTR_ERR(rk_pcie->rsts[i].rst);
		}
	}

	for (i = 0; i < count; i++) {
		ret = reset_control_deassert(rk_pcie->rsts[i].rst);
		if (ret) {
			dev_err(dev, "failed to release %s\n",
				rk_pcie->rsts[i].id);
			return ret;
		}
	}

	return 0;
}

static int rk_pcie_reset_grant_ctrl(struct rk_pcie *rk_pcie,
						bool enable)
{
	int ret;
	u32 val = (0x1 << 18); /* Write mask bit */

	if (enable)
		val |= (0x1 << 2);

	ret = regmap_write(rk_pcie->usb_pcie_grf, 0x0, val);
	return ret;
}

void rk_pcie_start_dma_1808(struct dma_trx_obj *obj)
{
	struct rk_pcie *rk_pcie = dev_get_drvdata(obj->dev);

	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_WR_ENB,
		obj->cur->wr_enb.asdword);
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_CTRL_LO,
		obj->cur->ctx_reg.ctrllo.asdword);
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_CTRL_HI,
		obj->cur->ctx_reg.ctrlhi.asdword);
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_XFERSIZE,
		obj->cur->ctx_reg.xfersize);
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_SAR_PTR_LO,
		obj->cur->ctx_reg.sarptrlo);
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_SAR_PTR_HI,
		obj->cur->ctx_reg.sarptrhi);
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_DAR_PTR_LO,
		obj->cur->ctx_reg.darptrlo);
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_DAR_PTR_HI,
		obj->cur->ctx_reg.darptrhi);
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_WR_WEILO,
		obj->cur->wr_weilo.asdword);
	rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET + PCIE_DMA_WR_DOORBELL,
		obj->cur->start.asdword);
}
EXPORT_SYMBOL(rk_pcie_start_dma_1808);

static inline void
rk_pcie_handle_dma_interrupt(struct rk_pcie *rk_pcie)
{
	struct dma_trx_obj *obj = rk_pcie->dma_obj;

	if (!obj)
		return;

	obj->dma_free = true;
	obj->irq_num++;

	if (list_empty(&obj->tbl_list)) {
		if (obj->dma_free &&
		    obj->loop_count >= obj->loop_count_threshold)
			complete(&obj->done);
	}
}

static irqreturn_t rk_pcie_sys_irq_handler(int irq, void *arg)
{
	struct rk_pcie *rk_pcie = arg;
	u32 chn = rk_pcie->dma_obj->cur->chn;
	union int_status status;
	union int_clear clears;

	status.asdword = rk_pcie_readl_dbi(rk_pcie, PCIE_DMA_OFFSET +
					   PCIE_DMA_WR_INT_STATUS);

	if (status.donesta & BIT(0)) {
		rk_pcie_handle_dma_interrupt(rk_pcie);
		clears.doneclr = 0x1 << chn;
		rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET +
				   PCIE_DMA_WR_INT_CLEAR, clears.asdword);
	}

	if (status.abortsta & BIT(0)) {
		dev_err(rk_pcie->dev, "%s, abort\n", __func__);
		clears.abortclr = 0x1 << chn;
		rk_pcie_writel_dbi(rk_pcie, PCIE_DMA_OFFSET +
				   PCIE_DMA_WR_INT_CLEAR, clears.asdword);
	}

	return IRQ_HANDLED;
}

static int rk_pcie_request_sys_irq(struct rk_pcie *rk_pcie,
					struct platform_device *pdev)
{
	int irq;
	int ret;

	irq = platform_get_irq_byname(pdev, "sys");
	if (irq < 0) {
		dev_err(rk_pcie->dev, "missing sys IRQ resource\n");
		return -EINVAL;
	}

	ret = devm_request_irq(rk_pcie->dev, irq, rk_pcie_sys_irq_handler,
			       IRQF_SHARED, "pcie-sys", rk_pcie);
	if (ret) {
		dev_err(rk_pcie->dev, "failed to request PCIe subsystem IRQ\n");
		return ret;
	}

	return 0;
}

static const struct rk_pcie_of_data rk_pcie_rc_of_data = {
	.mode = RK_PCIE_RC_TYPE,
};

static const struct rk_pcie_of_data rk_pcie_ep_of_data = {
	.mode = RK_PCIE_EP_TYPE,
};

static const struct of_device_id rk_pcie_of_match[] = {
	{
		.compatible = "rockchip,rk1808-pcie",
		.data = &rk_pcie_rc_of_data,
	},
	{
		.compatible = "rockchip,rk1808-pcie-ep",
		.data = &rk_pcie_ep_of_data,
	},
	{},
};

static int rk_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rk_pcie *rk_pcie;
	int ret;
	const struct of_device_id *match;
	const struct rk_pcie_of_data *data;
	enum rk_pcie_device_mode mode;

	match = of_match_device(rk_pcie_of_match, dev);
	if (!match)
		return -EINVAL;

	data = (struct rk_pcie_of_data *)match->data;
	mode = (enum rk_pcie_device_mode)data->mode;

	rk_pcie = devm_kzalloc(dev, sizeof(*rk_pcie), GFP_KERNEL);
	if (!rk_pcie)
		return -ENOMEM;
	rk_pcie->mode = mode;
	rk_pcie->dev = dev;

	ret = rk_pcie_resource_get(pdev, rk_pcie);
	if (ret) {
		dev_err(dev, "resource init failed\n");
		return ret;
	}

	ret = rk_pcie_phy_init(rk_pcie);
	if (ret) {
		dev_err(dev, "phy init failed\n");
		return ret;
	}

	ret = rk_pcie_reset_control_release(rk_pcie);
	if (ret) {
		dev_err(dev, "reset control init failed\n");
		return ret;
	}

	ret = rk_pcie_request_sys_irq(rk_pcie, pdev);
	if (ret) {
		dev_err(dev, "pcie irq init failed\n");
		return ret;
	}

	rk_pcie->usb_pcie_grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							 "rockchip,usbpciegrf");
	if (IS_ERR(rk_pcie->usb_pcie_grf)) {
		dev_err(dev, "failed to find usb_pcie_grf regmap\n");
		return PTR_ERR(rk_pcie->usb_pcie_grf);
	}

	rk_pcie->pmu_grf = syscon_regmap_lookup_by_phandle(dev->of_node,
							 "rockchip,pmugrf");
	if (IS_ERR(rk_pcie->pmu_grf)) {
		dev_err(dev, "failed to find pmugrf regmap\n");
		return PTR_ERR(rk_pcie->pmu_grf);
	}

	/* Workaround for pcie, switch to PCIe_PRSTNm0 */
	ret = regmap_write(rk_pcie->pmu_grf, 0x100, 0x01000100);
	if (ret)
		return ret;

	ret = regmap_write(rk_pcie->pmu_grf, 0x0, 0x0c000000);
	if (ret)
		return ret;

	ret = rk_pcie_clk_init(rk_pcie);
	if (ret) {
		dev_err(dev, "clock init failed\n");
		return ret;
	}

	platform_set_drvdata(pdev, rk_pcie);

	rk_pcie_dbi_ro_wr_en(rk_pcie);

	/* release link reset grant */
	ret = rk_pcie_reset_grant_ctrl(rk_pcie, true);
	if (ret)
		goto deinit_clk;

	/* Set PCIe mode */
	rk_pcie_set_mode(rk_pcie);
	/* Set PCIe gen2 */
	rk_pcie_set_gens(rk_pcie);

	ret = rk_pcie_establish_link(rk_pcie);
	if (ret) {
		dev_err(dev, "failed to establish pcie link\n");
		goto deinit_clk;
	}

	switch (rk_pcie->mode) {
	case RK_PCIE_RC_TYPE:
		ret = rk_pcie_add_host(rk_pcie, pdev);
		break;
	case RK_PCIE_EP_TYPE:
		ret = rk_pcie_add_ep(rk_pcie, pdev);
		break;
	}

	if (ret)
		goto deinit_clk;

	/* hold link reset grant after link-up */
	ret = rk_pcie_reset_grant_ctrl(rk_pcie, false);
	if (ret)
		goto deinit_clk;

	rk_pcie_dbi_ro_wr_dis(rk_pcie);

	return 0;

deinit_clk:
	rk_pcie_clk_deinit(rk_pcie);

	return ret;
}

MODULE_DEVICE_TABLE(of, rk_pcie_of_match);

static struct platform_driver rk_plat_pcie_driver = {
	.driver = {
		.name	= "rk-pcie",
		.of_match_table = rk_pcie_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver_probe(rk_plat_pcie_driver, rk_pcie_probe);

MODULE_AUTHOR("Simon Xue <xxm@rock-chips.com>");
MODULE_DESCRIPTION("RockChip PCIe Controller driver");
MODULE_LICENSE("GPL v2");
