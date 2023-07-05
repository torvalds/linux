// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe EP controller driver for Rockchip SoCs
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 *		http://www.rock-chips.com
 *
 * Author: Simon Xue <xxm@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/miscdevice.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/uaccess.h>
#include <uapi/linux/rk-pcie-ep.h>

#include "../rockchip-pcie-dma.h"
#include "pcie-designware.h"
#include "pcie-dw-dmatest.h"

/*
 * The upper 16 bits of PCIE_CLIENT_CONFIG are a write
 * mask for the lower 16 bits.
 */
#define HIWORD_UPDATE(mask, val) (((mask) << 16) | (val))
#define HIWORD_UPDATE_BIT(val)	HIWORD_UPDATE(val, val)

#define to_rockchip_pcie(x) dev_get_drvdata((x)->dev)

#define PCIE_DMA_OFFSET			0x380000

#define PCIE_DMA_CTRL_OFF		0x8
#define PCIE_DMA_WR_ENB			0xc
#define PCIE_DMA_WR_CTRL_LO		0x200
#define PCIE_DMA_WR_CTRL_HI		0x204
#define PCIE_DMA_WR_XFERSIZE		0x208
#define PCIE_DMA_WR_SAR_PTR_LO		0x20c
#define PCIE_DMA_WR_SAR_PTR_HI		0x210
#define PCIE_DMA_WR_DAR_PTR_LO		0x214
#define PCIE_DMA_WR_DAR_PTR_HI		0x218
#define PCIE_DMA_WR_WEILO		0x18
#define PCIE_DMA_WR_WEIHI		0x1c
#define PCIE_DMA_WR_DOORBELL		0x10
#define PCIE_DMA_WR_INT_STATUS		0x4c
#define PCIE_DMA_WR_INT_MASK		0x54
#define PCIE_DMA_WR_INT_CLEAR		0x58

#define PCIE_DMA_RD_ENB			0x2c
#define PCIE_DMA_RD_CTRL_LO		0x300
#define PCIE_DMA_RD_CTRL_HI		0x304
#define PCIE_DMA_RD_XFERSIZE		0x308
#define PCIE_DMA_RD_SAR_PTR_LO		0x30c
#define PCIE_DMA_RD_SAR_PTR_HI		0x310
#define PCIE_DMA_RD_DAR_PTR_LO		0x314
#define PCIE_DMA_RD_DAR_PTR_HI		0x318
#define PCIE_DMA_RD_WEILO		0x38
#define PCIE_DMA_RD_WEIHI		0x3c
#define PCIE_DMA_RD_DOORBELL		0x30
#define PCIE_DMA_RD_INT_STATUS		0xa0
#define PCIE_DMA_RD_INT_MASK		0xa8
#define PCIE_DMA_RD_INT_CLEAR		0xac

#define PCIE_DMA_CHANEL_MAX_NUM		2

#define PCIE_CLIENT_RC_MODE		HIWORD_UPDATE_BIT(0x40)
#define PCIE_CLIENT_ENABLE_LTSSM	HIWORD_UPDATE_BIT(0xc)
#define PCIE_CLIENT_INTR_STATUS_MISC	0x10
#define PCIE_SMLH_LINKUP		BIT(16)
#define PCIE_RDLH_LINKUP		BIT(17)
#define PCIE_L0S_ENTRY			0x11
#define PCIE_CLIENT_GENERAL_CONTROL	0x0
#define PCIE_CLIENT_GENERAL_DEBUG	0x104
#define PCIE_CLIENT_HOT_RESET_CTRL      0x180
#define PCIE_CLIENT_LTSSM_STATUS	0x300
#define PCIE_CLIENT_INTR_MASK		0x24
#define PCIE_LTSSM_APP_DLY1_EN		BIT(0)
#define PCIE_LTSSM_APP_DLY2_EN		BIT(1)
#define PCIE_LTSSM_APP_DLY1_DONE	BIT(2)
#define PCIE_LTSSM_APP_DLY2_DONE	BIT(3)
#define PCIE_LTSSM_ENABLE_ENHANCE       BIT(4)
#define PCIE_CLIENT_MSI_GEN_CON		0x38

#define PCIe_CLIENT_MSI_OBJ_IRQ		0	/* rockchip ep object special irq */

#define PCIE_ELBI_REG_NUM		0x2
#define PCIE_ELBI_LOCAL_BASE		0x200e00

#define PCIE_ELBI_APP_ELBI_INT_GEN0		0x0
#define PCIE_ELBI_APP_ELBI_INT_GEN0_SIGIO	BIT(0)

#define PCIE_ELBI_APP_ELBI_INT_GEN1		0x4

#define PCIE_ELBI_LOCAL_ENABLE_OFF	0x8

#define PCIE_DIRECT_SPEED_CHANGE	BIT(17)

#define PCIE_TYPE0_STATUS_COMMAND_REG	0x4
#define PCIE_TYPE0_HDR_DBI2_OFFSET	0x100000

#define PCIE_DBI_SIZE			0x400000

#define PCIE_EP_OBJ_INFO_DRV_VERSION	0x00000001

#define PCIE_BAR_MAX_NUM		6
#define PCIE_HOTRESET_TMOUT_US		10000

struct rockchip_pcie {
	struct dw_pcie			pci;
	void __iomem			*apb_base;
	struct phy			*phy;
	struct clk_bulk_data		*clks;
	unsigned int			clk_cnt;
	struct reset_control		*rst;
	struct gpio_desc		*rst_gpio;
	struct regulator                *vpcie3v3;
	unsigned long			*ib_window_map;
	unsigned long			*ob_window_map;
	u32				num_ib_windows;
	u32				num_ob_windows;
	phys_addr_t			*outbound_addr;
	u8				bar_to_atu[PCIE_BAR_MAX_NUM];
	dma_addr_t			ib_target_address[PCIE_BAR_MAX_NUM];
	u32				ib_target_size[PCIE_BAR_MAX_NUM];
	void				*ib_target_base[PCIE_BAR_MAX_NUM];
	struct dma_trx_obj		*dma_obj;
	struct fasync_struct		*async;
	phys_addr_t			dbi_base_physical;
	struct pcie_ep_obj_info		*obj_info;
	enum pcie_ep_mmap_resource	cur_mmap_res;
	struct workqueue_struct		*hot_rst_wq;
	struct work_struct		hot_rst_work;
};

struct rockchip_pcie_misc_dev {
	struct miscdevice dev;
	struct rockchip_pcie *pcie;
};

static const struct of_device_id rockchip_pcie_ep_of_match[] = {
	{
		.compatible = "rockchip,rk3568-pcie-std-ep",
	},
	{
		.compatible = "rockchip,rk3588-pcie-std-ep",
	},
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_pcie_ep_of_match);

static void rockchip_pcie_devmode_update(struct rockchip_pcie *rockchip, int mode, int submode)
{
	rockchip->obj_info->devmode.mode = mode;
	rockchip->obj_info->devmode.submode = submode;
}

static int rockchip_pcie_readl_apb(struct rockchip_pcie *rockchip, u32 reg)
{
	return readl(rockchip->apb_base + reg);
}

static void rockchip_pcie_writel_apb(struct rockchip_pcie *rockchip, u32 val, u32 reg)
{
	writel(val, rockchip->apb_base + reg);
}

static void *rockchip_pcie_map_kernel(phys_addr_t start, size_t len)
{
	int i;
	void *vaddr;
	pgprot_t pgprot;
	phys_addr_t phys;
	int npages = PAGE_ALIGN(len) / PAGE_SIZE;
	struct page **p = vmalloc(sizeof(struct page *) * npages);

	if (!p)
		return NULL;

	pgprot = pgprot_noncached(PAGE_KERNEL);

	phys = start;
	for (i = 0; i < npages; i++) {
		p[i] = phys_to_page(phys);
		phys += PAGE_SIZE;
	}

	vaddr = vmap(p, npages, VM_MAP, pgprot);
	vfree(p);

	return vaddr;
}

static int rockchip_pcie_resource_get(struct platform_device *pdev,
				      struct rockchip_pcie *rockchip)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void *addr;
	struct resource *dbi_base;
	struct device_node *mem;
	struct resource reg;
	char name[8];
	int i, idx;

	dbi_base = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"pcie-dbi");
	if (!dbi_base) {
		dev_err(&pdev->dev, "get pcie-dbi failed\n");
		return -ENODEV;
	}

	rockchip->pci.dbi_base = devm_ioremap_resource(dev, dbi_base);
	if (IS_ERR(rockchip->pci.dbi_base))
		return PTR_ERR(rockchip->pci.dbi_base);
	rockchip->pci.atu_base = rockchip->pci.dbi_base + DEFAULT_DBI_ATU_OFFSET;
	rockchip->dbi_base_physical = dbi_base->start;

	rockchip->apb_base = devm_platform_ioremap_resource_byname(pdev, "pcie-apb");
	if (!rockchip->apb_base) {
		dev_err(dev, "get pcie-apb failed\n");
		return -ENODEV;
	}

	rockchip->rst_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(rockchip->rst_gpio))
		return PTR_ERR(rockchip->rst_gpio);

	ret = device_property_read_u32(dev, "num-ib-windows", &rockchip->num_ib_windows);
	if (ret < 0) {
		dev_err(dev, "unable to read *num-ib-windows* property\n");
		return ret;
	}

	if (rockchip->num_ib_windows > MAX_IATU_IN) {
		dev_err(dev, "Invalid *num-ib-windows*\n");
		return -EINVAL;
	}

	ret = device_property_read_u32(dev, "num-ob-windows", &rockchip->num_ob_windows);
	if (ret < 0) {
		dev_err(dev, "Unable to read *num-ob-windows* property\n");
		return ret;
	}

	if (rockchip->num_ob_windows > MAX_IATU_OUT) {
		dev_err(dev, "Invalid *num-ob-windows*\n");
		return -EINVAL;
	}

	rockchip->ib_window_map = devm_kcalloc(dev,
					BITS_TO_LONGS(rockchip->num_ib_windows),
					sizeof(long), GFP_KERNEL);
	if (!rockchip->ib_window_map)
		return -ENOMEM;

	rockchip->ob_window_map = devm_kcalloc(dev,
					BITS_TO_LONGS(rockchip->num_ob_windows),
					sizeof(long), GFP_KERNEL);
	if (!rockchip->ob_window_map)
		return -ENOMEM;

	addr = devm_kcalloc(dev, rockchip->num_ob_windows, sizeof(phys_addr_t),
			    GFP_KERNEL);
	if (!addr)
		return -ENOMEM;

	rockchip->outbound_addr = addr;

	for (i = 0; i < PCIE_BAR_MAX_NUM; i++) {
		snprintf(name, sizeof(name), "bar%d", i);
		idx = of_property_match_string(np, "memory-region-names", name);
		if (idx < 0)
			continue;

		mem = of_parse_phandle(np, "memory-region", idx);
		if (!mem) {
			dev_err(dev, "missing \"memory-region\" %s property\n", name);
			return -ENODEV;
		}

		ret = of_address_to_resource(mem, 0, &reg);
		if (ret < 0) {
			dev_err(dev, "missing \"reg\" %s property\n", name);
			return -ENODEV;
		}

		rockchip->ib_target_address[i] = reg.start;
		rockchip->ib_target_size[i] = resource_size(&reg);
		rockchip->ib_target_base[i] = rockchip_pcie_map_kernel(reg.start,
							resource_size(&reg));
		dev_info(dev, "%s: assigned [0x%llx-%llx]\n", name, rockchip->ib_target_address[i],
			rockchip->ib_target_address[i] + rockchip->ib_target_size[i] - 1);
	}

	if (rockchip->ib_target_size[0]) {
		rockchip->obj_info = (struct pcie_ep_obj_info *)rockchip->ib_target_base[0];
		memset_io(rockchip->obj_info, 0, sizeof(struct pcie_ep_obj_info));
		rockchip->obj_info->magic = PCIE_EP_OBJ_INFO_MAGIC;
		rockchip->obj_info->version = PCIE_EP_OBJ_INFO_DRV_VERSION;
		rockchip_pcie_devmode_update(rockchip, RKEP_MODE_KERNEL, RKEP_SMODE_INIT);
	} else {
		dev_err(dev, "missing bar0 memory region\n");
		return -ENODEV;
	}

	return 0;
}

static void rockchip_pcie_enable_ltssm(struct rockchip_pcie *rockchip)
{
	/* Set ep mode */
	rockchip_pcie_writel_apb(rockchip, 0xf00000, 0x0);
	rockchip_pcie_writel_apb(rockchip, PCIE_CLIENT_ENABLE_LTSSM,
				 PCIE_CLIENT_GENERAL_CONTROL);
}

static int rockchip_pcie_link_up(struct dw_pcie *pci)
{
	struct rockchip_pcie *rockchip = to_rockchip_pcie(pci);
	u32 val = rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_LTSSM_STATUS);

	if ((val & (PCIE_RDLH_LINKUP | PCIE_SMLH_LINKUP)) == 0x30000)
		return 1;

	return 0;
}

static int rockchip_pcie_start_link(struct dw_pcie *pci)
{
	struct rockchip_pcie *rockchip = to_rockchip_pcie(pci);

	/* Reset device */
	gpiod_set_value_cansleep(rockchip->rst_gpio, 0);
	msleep(100);
	gpiod_set_value_cansleep(rockchip->rst_gpio, 1);

	rockchip_pcie_enable_ltssm(rockchip);

	return 0;
}

static int rockchip_pcie_phy_init(struct rockchip_pcie *rockchip)
{
	int ret;
	struct device *dev = rockchip->pci.dev;

	rockchip->phy = devm_phy_get(dev, "pcie-phy");
	if (IS_ERR(rockchip->phy)) {
		dev_err(dev, "missing phy\n");
		return PTR_ERR(rockchip->phy);
	}

	ret = phy_init(rockchip->phy);
	if (ret < 0)
		return ret;

	phy_power_on(rockchip->phy);

	return 0;
}

static void rockchip_pcie_phy_deinit(struct rockchip_pcie *rockchip)
{
	phy_exit(rockchip->phy);
	phy_power_off(rockchip->phy);
}

static int rockchip_pcie_reset_control_release(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->pci.dev;
	int ret;

	rockchip->rst = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(rockchip->rst)) {
		dev_err(dev, "failed to get reset lines\n");
		return PTR_ERR(rockchip->rst);
	}

	ret = reset_control_deassert(rockchip->rst);

	return ret;
}

static int rockchip_pcie_clk_init(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->pci.dev;
	int ret;

	ret = devm_clk_bulk_get_all(dev, &rockchip->clks);
	if (ret < 0)
		return ret;

	rockchip->clk_cnt = ret;

	ret = clk_bulk_prepare_enable(rockchip->clk_cnt, rockchip->clks);
	if (ret)
		return ret;

	return 0;
}

static int rockchip_pci_find_resbar_capability(struct rockchip_pcie *rockchip)
{
	u32 header;
	int ttl;
	int start = 0;
	int pos = PCI_CFG_SPACE_SIZE;
	int cap = PCI_EXT_CAP_ID_REBAR;

	/* minimum 8 bytes per capability */
	ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

	header = dw_pcie_readl_dbi(&rockchip->pci, pos);

	/*
	 * If we have no capabilities, this is indicated by cap ID,
	 * cap version and next pointer all being 0.
	 */
	if (header == 0)
		return 0;

	while (ttl-- > 0) {
		if (PCI_EXT_CAP_ID(header) == cap && pos != start)
			return pos;

		pos = PCI_EXT_CAP_NEXT(header);
		if (pos < PCI_CFG_SPACE_SIZE)
			break;

		header = dw_pcie_readl_dbi(&rockchip->pci, pos);
		if (!header)
			break;
	}

	return 0;
}

static int rockchip_pcie_ep_set_bar_flag(struct rockchip_pcie *rockchip, enum pci_barno barno,
					 int flags)
{
	enum pci_barno bar = barno;
	u32 reg;

	reg = PCI_BASE_ADDRESS_0 + (4 * bar);

	/* Disabled the upper 32bits BAR to make a 64bits bar pair */
	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64)
		dw_pcie_writel_dbi(&rockchip->pci, reg + PCIE_TYPE0_HDR_DBI2_OFFSET + 4, 0);

	dw_pcie_writel_dbi(&rockchip->pci, reg, flags);
	if (flags & PCI_BASE_ADDRESS_MEM_TYPE_64)
		dw_pcie_writel_dbi(&rockchip->pci, reg + 4, 0);

	return 0;
}

static void rockchip_pcie_resize_bar(struct rockchip_pcie *rockchip)
{
	struct dw_pcie *pci = &rockchip->pci;
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;
	int bar, ret;
	u32 resbar_base, lanes, val;

	ret = of_property_read_u32(np, "num-lanes", &lanes);
	if (ret)
		lanes = 0;

	/* Set the number of lanes */
	val = dw_pcie_readl_dbi(pci, PCIE_PORT_LINK_CONTROL);
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
		dev_err(dev, "num-lanes %u: invalid value\n", lanes);
		return;
	}

	dw_pcie_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, val);

	/* Set link width speed control register */
	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
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

	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	/* Enable bus master and memory space */
	dw_pcie_writel_dbi(pci, PCIE_TYPE0_STATUS_COMMAND_REG, 0x6);

	resbar_base = rockchip_pci_find_resbar_capability(rockchip);

	/* Resize BAR0 4M 32bits, BAR2 64M 64bits-pref, BAR4 1MB 32bits */
	bar = BAR_0;
	dw_pcie_writel_dbi(pci, resbar_base + 0x4 + bar * 0x8, 0xfffff0);
	dw_pcie_writel_dbi(pci, resbar_base + 0x8 + bar * 0x8, 0x2c0);
	rockchip_pcie_ep_set_bar_flag(rockchip, bar, PCI_BASE_ADDRESS_MEM_TYPE_32);

	bar = BAR_2;
	dw_pcie_writel_dbi(pci, resbar_base + 0x4 + bar * 0x8, 0xfffff0);
	dw_pcie_writel_dbi(pci, resbar_base + 0x8 + bar * 0x8, 0x6c0);
	rockchip_pcie_ep_set_bar_flag(rockchip, bar,
				      PCI_BASE_ADDRESS_MEM_PREFETCH | PCI_BASE_ADDRESS_MEM_TYPE_64);

	bar = BAR_4;
	dw_pcie_writel_dbi(pci, resbar_base + 0x4 + bar * 0x8, 0xfffff0);
	dw_pcie_writel_dbi(pci, resbar_base + 0x8 + bar * 0x8, 0xc0);
	rockchip_pcie_ep_set_bar_flag(rockchip, bar, PCI_BASE_ADDRESS_MEM_TYPE_32);

	/* Disable BAR1 BAR5*/
	bar = BAR_1;
	dw_pcie_writel_dbi(pci, PCIE_TYPE0_HDR_DBI2_OFFSET + 0x10 + bar * 4, 0);
	bar = BAR_5;
	dw_pcie_writel_dbi(pci, PCIE_TYPE0_HDR_DBI2_OFFSET + 0x10 + bar * 4, 0);
}

static void rockchip_pcie_init_id(struct rockchip_pcie *rockchip)
{
	struct dw_pcie *pci = &rockchip->pci;

	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID, 0x356a);
	dw_pcie_writew_dbi(pci, PCI_CLASS_DEVICE, 0x0580);
}

static int rockchip_pcie_ep_set_bar(struct rockchip_pcie *rockchip, enum pci_barno bar,
				    dma_addr_t cpu_addr)
{
	int ret;
	u32 free_win;
	struct dw_pcie *pci = &rockchip->pci;
	enum dw_pcie_as_type as_type;

	free_win = find_first_zero_bit(rockchip->ib_window_map,
				       rockchip->num_ib_windows);
	if (free_win >= rockchip->num_ib_windows) {
		dev_err(pci->dev, "No free inbound window\n");
		return -EINVAL;
	}

	as_type = DW_PCIE_AS_MEM;

	ret = dw_pcie_prog_inbound_atu(pci, 0, free_win, bar, cpu_addr, as_type);
	if (ret < 0) {
		dev_err(pci->dev, "Failed to program IB window\n");
		return ret;
	}

	rockchip->bar_to_atu[bar] = free_win;
	set_bit(free_win, rockchip->ib_window_map);

	return 0;
}

static void rockchip_pcie_fast_link_setup(struct rockchip_pcie *rockchip)
{
	u32 val;

	/* LTSSM EN ctrl mode */
	val = rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_HOT_RESET_CTRL);
	val |= (PCIE_LTSSM_ENABLE_ENHANCE | PCIE_LTSSM_APP_DLY2_EN) |
		((PCIE_LTSSM_ENABLE_ENHANCE | PCIE_LTSSM_APP_DLY2_EN) << 16);
	rockchip_pcie_writel_apb(rockchip, val, PCIE_CLIENT_HOT_RESET_CTRL);
}

static u8 rockchip_pcie_iatu_unroll_enabled(struct dw_pcie *pci)
{
	u32 val;

	val = dw_pcie_readl_dbi(pci, PCIE_ATU_VIEWPORT);
	if (val == 0xffffffff)
		return 1;

	return 0;
}

static void rockchip_pcie_local_elbi_enable(struct rockchip_pcie *rockchip)
{
	int i;
	u32 elbi_reg;
	struct dw_pcie *pci = &rockchip->pci;

	for (i = 0; i < PCIE_ELBI_REG_NUM; i++) {
		elbi_reg = PCIE_ELBI_LOCAL_BASE + PCIE_ELBI_LOCAL_ENABLE_OFF +
			   i * 4;
		dw_pcie_writel_dbi(pci, elbi_reg, 0xffff0000);
	}
}

static void rockchip_pcie_elbi_clear(struct rockchip_pcie *rockchip)
{
	int i;
	u32 elbi_reg;
	struct dw_pcie *pci = &rockchip->pci;
	u32 val;

	for (i = 0; i < PCIE_ELBI_REG_NUM; i++) {
		elbi_reg = PCIE_ELBI_LOCAL_BASE + i * 4;
		val = dw_pcie_readl_dbi(pci, elbi_reg);
		val <<= 16;
		dw_pcie_writel_dbi(pci, elbi_reg, val);
	}
}

static void rockchip_pcie_raise_msi_irq(struct rockchip_pcie *rockchip, u8 interrupt_num)
{
	rockchip_pcie_writel_apb(rockchip, BIT(interrupt_num), PCIE_CLIENT_MSI_GEN_CON);
}

static irqreturn_t rockchip_pcie_sys_irq_handler(int irq, void *arg)
{
	struct rockchip_pcie *rockchip = arg;
	struct dw_pcie *pci = &rockchip->pci;
	u32 elbi_reg;
	u32 chn;
	union int_status wr_status, rd_status;
	union int_clear clears;
	u32 reg, mask;
	bool sigio = false;

	/* ELBI helper, only check the valid bits, and discard the rest interrupts */
	elbi_reg = dw_pcie_readl_dbi(pci, PCIE_ELBI_LOCAL_BASE + PCIE_ELBI_APP_ELBI_INT_GEN0);
	if (elbi_reg & PCIE_ELBI_APP_ELBI_INT_GEN0_SIGIO) {
		sigio = true;
		rockchip->obj_info->irq_type_ep = OBJ_IRQ_ELBI;
		rockchip_pcie_elbi_clear(rockchip);
		goto out;
	}

	/* DMA helper */
	mask = dw_pcie_readl_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_MASK);
	wr_status.asdword = dw_pcie_readl_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_STATUS) & (~mask);
	mask = dw_pcie_readl_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_MASK);
	rd_status.asdword = dw_pcie_readl_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_STATUS) & (~mask);

	for (chn = 0; chn < PCIE_DMA_CHANEL_MAX_NUM; chn++) {
		if (wr_status.donesta & BIT(chn)) {
			clears.doneclr = BIT(chn);
			dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET +
					PCIE_DMA_WR_INT_CLEAR, clears.asdword);
			if (rockchip->dma_obj && rockchip->dma_obj->cb)
				rockchip->dma_obj->cb(rockchip->dma_obj, chn, DMA_TO_BUS);
		}

		if (wr_status.abortsta & BIT(chn)) {
			dev_err(pci->dev, "%s, abort\n", __func__);
			clears.abortclr = BIT(chn);
			dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET +
					PCIE_DMA_WR_INT_CLEAR, clears.asdword);
		}
	}

	for (chn = 0; chn < PCIE_DMA_CHANEL_MAX_NUM; chn++) {
		if (rd_status.donesta & BIT(chn)) {
			clears.doneclr = BIT(chn);
			dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET +
					PCIE_DMA_RD_INT_CLEAR, clears.asdword);
			if (rockchip->dma_obj && rockchip->dma_obj->cb)
				rockchip->dma_obj->cb(rockchip->dma_obj, chn, DMA_FROM_BUS);
		}

		if (rd_status.abortsta & BIT(chn)) {
			dev_err(pci->dev, "%s, abort\n", __func__);
			clears.abortclr = BIT(chn);
			dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET +
					PCIE_DMA_RD_INT_CLEAR, clears.asdword);
		}
	}

	if (wr_status.asdword || rd_status.asdword) {
		rockchip->obj_info->irq_type_rc = OBJ_IRQ_DMA;
		rockchip->obj_info->dma_status_rc.wr |= wr_status.asdword;
		rockchip->obj_info->dma_status_rc.rd |= rd_status.asdword;
		rockchip_pcie_raise_msi_irq(rockchip, PCIe_CLIENT_MSI_OBJ_IRQ);

		rockchip->obj_info->irq_type_ep = OBJ_IRQ_DMA;
		rockchip->obj_info->dma_status_ep.wr |= wr_status.asdword;
		rockchip->obj_info->dma_status_ep.rd |= rd_status.asdword;
		sigio = true;
	}

out:
	if (sigio) {
		dev_dbg(rockchip->pci.dev, "SIGIO\n");
		kill_fasync(&rockchip->async, SIGIO, POLL_IN);
	}

	reg = rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_INTR_STATUS_MISC);
	if (reg & BIT(2))
		queue_work(rockchip->hot_rst_wq, &rockchip->hot_rst_work);

	rockchip_pcie_writel_apb(rockchip, reg, PCIE_CLIENT_INTR_STATUS_MISC);

	return IRQ_HANDLED;
}

static int rockchip_pcie_request_sys_irq(struct rockchip_pcie *rockchip,
					struct platform_device *pdev)
{
	int irq;
	int ret;
	struct device *dev = rockchip->pci.dev;

	irq = platform_get_irq_byname(pdev, "sys");
	if (irq < 0) {
		dev_err(dev, "missing sys IRQ resource\n");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, irq, rockchip_pcie_sys_irq_handler,
			       IRQF_SHARED, "pcie-sys", rockchip);
	if (ret) {
		dev_err(dev, "failed to request PCIe subsystem IRQ\n");
		return ret;
	}

	return 0;
}

static bool rockchip_pcie_udma_enabled(struct rockchip_pcie *rockchip)
{
	struct dw_pcie *pci = &rockchip->pci;

	return dw_pcie_readl_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_CTRL_OFF);
}

static int rockchip_pcie_init_dma_trx(struct rockchip_pcie *rockchip)
{
	struct dw_pcie *pci = &rockchip->pci;

	if (!rockchip_pcie_udma_enabled(rockchip))
		return 0;

	rockchip->dma_obj = pcie_dw_dmatest_register(pci->dev, true);
	if (IS_ERR(rockchip->dma_obj)) {
		dev_err(rockchip->pci.dev, "failed to prepare dmatest\n");
		return -EINVAL;
	}

	/* Enable client write and read interrupt */
	rockchip_pcie_writel_apb(rockchip, 0xc000000, PCIE_CLIENT_INTR_MASK);

	/* Enable core write interrupt */
	dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_MASK, 0x0);
	/* Enable core read interrupt */
	dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_MASK, 0x0);

	return 0;
}

static void rockchip_pcie_start_dma_rd(struct dma_trx_obj *obj, struct dma_table *cur, int ctr_off)
{
	struct rockchip_pcie *rockchip = dev_get_drvdata(obj->dev);
	struct dw_pcie *pci = &rockchip->pci;

	dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_RD_ENB,
			   cur->enb.asdword);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_RD_CTRL_LO,
			   cur->ctx_reg.ctrllo.asdword);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_RD_CTRL_HI,
			   cur->ctx_reg.ctrlhi.asdword);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_RD_XFERSIZE,
			   cur->ctx_reg.xfersize);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_RD_SAR_PTR_LO,
			   cur->ctx_reg.sarptrlo);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_RD_SAR_PTR_HI,
			   cur->ctx_reg.sarptrhi);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_RD_DAR_PTR_LO,
			   cur->ctx_reg.darptrlo);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_RD_DAR_PTR_HI,
			   cur->ctx_reg.darptrhi);
	dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_RD_DOORBELL,
			   cur->start.asdword);
}

static void rockchip_pcie_start_dma_wr(struct dma_trx_obj *obj, struct dma_table *cur, int ctr_off)
{
	struct rockchip_pcie *rockchip = dev_get_drvdata(obj->dev);
	struct dw_pcie *pci = &rockchip->pci;

	dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_WR_ENB,
			   cur->enb.asdword);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_WR_CTRL_LO,
			   cur->ctx_reg.ctrllo.asdword);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_WR_CTRL_HI,
			   cur->ctx_reg.ctrlhi.asdword);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_WR_XFERSIZE,
			   cur->ctx_reg.xfersize);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_WR_SAR_PTR_LO,
			   cur->ctx_reg.sarptrlo);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_WR_SAR_PTR_HI,
			   cur->ctx_reg.sarptrhi);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_WR_DAR_PTR_LO,
			   cur->ctx_reg.darptrlo);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_WR_DAR_PTR_HI,
			   cur->ctx_reg.darptrhi);
	dw_pcie_writel_dbi(pci, ctr_off + PCIE_DMA_WR_WEILO,
			   cur->weilo.asdword);
	dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_WR_DOORBELL,
			   cur->start.asdword);
}

static void rockchip_pcie_start_dma_dwc(struct dma_trx_obj *obj, struct dma_table *table)
{
	int dir = table->dir;
	int chn = table->chn;

	int ctr_off = PCIE_DMA_OFFSET + chn * 0x200;

	if (dir == DMA_FROM_BUS)
		rockchip_pcie_start_dma_rd(obj, table, ctr_off);
	else if (dir == DMA_TO_BUS)
		rockchip_pcie_start_dma_wr(obj, table, ctr_off);
}

static void rockchip_pcie_config_dma_dwc(struct dma_table *table)
{
	table->enb.enb = 0x1;
	table->ctx_reg.ctrllo.lie = 0x1;
	table->ctx_reg.ctrllo.rie = 0x0;
	table->ctx_reg.ctrllo.td = 0x1;
	table->ctx_reg.ctrlhi.asdword = 0x0;
	table->ctx_reg.xfersize = table->buf_size;
	if (table->dir == DMA_FROM_BUS) {
		table->ctx_reg.sarptrlo = (u32)(table->bus & 0xffffffff);
		table->ctx_reg.sarptrhi = (u32)(table->bus >> 32);
		table->ctx_reg.darptrlo = (u32)(table->local & 0xffffffff);
		table->ctx_reg.darptrhi = (u32)(table->local >> 32);
	} else if (table->dir == DMA_TO_BUS) {
		table->ctx_reg.sarptrlo = (u32)(table->local & 0xffffffff);
		table->ctx_reg.sarptrhi = (u32)(table->local >> 32);
		table->ctx_reg.darptrlo = (u32)(table->bus & 0xffffffff);
		table->ctx_reg.darptrhi = (u32)(table->bus >> 32);
	}
	table->weilo.weight0 = 0x0;
	table->start.stop = 0x0;
	table->start.chnl = table->chn;
}

static void rockchip_pcie_hot_rst_work(struct work_struct *work)
{
	struct rockchip_pcie *rockchip = container_of(work, struct rockchip_pcie, hot_rst_work);
	u32 status;
	int ret;

	if (rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_HOT_RESET_CTRL) & PCIE_LTSSM_APP_DLY2_EN) {
		ret = readl_poll_timeout(rockchip->apb_base + PCIE_CLIENT_LTSSM_STATUS,
			 status, ((status & 0x3F) == 0), 100, PCIE_HOTRESET_TMOUT_US);
		if (ret)
			dev_err(rockchip->pci.dev, "wait for detect quiet failed!\n");

		rockchip_pcie_writel_apb(rockchip, (PCIE_LTSSM_APP_DLY2_DONE) | ((PCIE_LTSSM_APP_DLY2_DONE) << 16),
					PCIE_CLIENT_HOT_RESET_CTRL);
	}
}

static int rockchip_pcie_get_dma_status(struct dma_trx_obj *obj, u8 chn, enum dma_dir dir)
{
	struct rockchip_pcie *rockchip = dev_get_drvdata(obj->dev);
	struct dw_pcie *pci = &rockchip->pci;
	union int_status status;
	union int_clear clears;
	int ret = 0;

	dev_dbg(pci->dev, "%s %x %x\n", __func__,
		dw_pcie_readl_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_STATUS),
		dw_pcie_readl_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_STATUS));

	if (dir == DMA_TO_BUS) {
		status.asdword = dw_pcie_readl_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_STATUS);
		if (status.donesta & BIT(chn)) {
			clears.doneclr = BIT(chn);
			dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET +
					   PCIE_DMA_WR_INT_CLEAR, clears.asdword);
			ret = 1;
		}

		if (status.abortsta & BIT(chn)) {
			dev_err(pci->dev, "%s, write abort\n", __func__);
			clears.abortclr = BIT(chn);
			dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET +
					   PCIE_DMA_WR_INT_CLEAR, clears.asdword);
			ret = -1;
		}
	} else {
		status.asdword = dw_pcie_readl_dbi(pci, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_STATUS);

		if (status.donesta & BIT(chn)) {
			clears.doneclr = BIT(chn);
			dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET +
					   PCIE_DMA_RD_INT_CLEAR, clears.asdword);
			ret = 1;
		}

		if (status.abortsta & BIT(chn)) {
			dev_err(pci->dev, "%s, read abort %x\n", __func__, status.asdword);
			clears.abortclr = BIT(chn);
			dw_pcie_writel_dbi(pci, PCIE_DMA_OFFSET +
					   PCIE_DMA_RD_INT_CLEAR, clears.asdword);
			ret = -1;
		}
	}

	return ret;
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.start_link = rockchip_pcie_start_link,
	.link_up = rockchip_pcie_link_up,
};

static int pcie_ep_fasync(int fd, struct file *file, int mode)
{
	struct rockchip_pcie *rockchip = (struct rockchip_pcie *)file->private_data;

	return fasync_helper(fd, file, mode, &rockchip->async);
}

static int pcie_ep_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct rockchip_pcie_misc_dev *pcie_misc_dev;

	pcie_misc_dev = container_of(miscdev, struct rockchip_pcie_misc_dev, dev);
	file->private_data = pcie_misc_dev->pcie;

	return 0;
}

static int pcie_ep_release(struct inode *inode, struct file *file)
{
	return pcie_ep_fasync(-1, file, 0);
}

static long pcie_ep_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rockchip_pcie *rockchip = (struct rockchip_pcie *)file->private_data;
	struct pcie_ep_user_data msg;
	struct pcie_ep_dma_cache_cfg cfg;
	void __user *uarg = (void __user *)arg;
	int i, ret;
	enum pcie_ep_mmap_resource mmap_res;

	switch (cmd) {
	case PCIE_DMA_GET_ELBI_DATA:
		for (i = 4; i <= 6; i++)
			msg.elbi_app_user[i - 4] = dw_pcie_readl_dbi(&rockchip->pci,
								     PCIE_ELBI_LOCAL_BASE + i * 4);
		for (i = 8; i <= 15; i++)
			msg.elbi_app_user[i - 5] = dw_pcie_readl_dbi(&rockchip->pci,
								     PCIE_ELBI_LOCAL_BASE + i * 4);

		ret = copy_to_user(uarg, &msg, sizeof(msg));
		if (ret) {
			dev_err(rockchip->pci.dev, "failed to get elbi data\n");
			return -EFAULT;
		}
		break;
	case PCIE_DMA_CACHE_INVALIDE:
		ret = copy_from_user(&cfg, uarg, sizeof(cfg));
		if (ret) {
			dev_err(rockchip->pci.dev, "failed to get copy from\n");
			return -EFAULT;
		}
		dma_sync_single_for_cpu(rockchip->pci.dev, cfg.addr, cfg.size, DMA_FROM_DEVICE);
		break;
	case PCIE_DMA_CACHE_FLUSH:
		ret = copy_from_user(&cfg, uarg, sizeof(cfg));
		if (ret) {
			dev_err(rockchip->pci.dev, "failed to get copy from\n");
			return -EFAULT;
		}
		dma_sync_single_for_device(rockchip->pci.dev, cfg.addr, cfg.size, DMA_TO_DEVICE);
		break;
	case PCIE_DMA_IRQ_MASK_ALL:
		dw_pcie_writel_dbi(&rockchip->pci, PCIE_DMA_OFFSET + PCIE_DMA_WR_INT_MASK,
				   0xffffffff);
		dw_pcie_writel_dbi(&rockchip->pci, PCIE_DMA_OFFSET + PCIE_DMA_RD_INT_MASK,
				   0xffffffff);
		break;
	case PCIE_DMA_RAISE_MSI_OBJ_IRQ_USER:
		rockchip->obj_info->irq_type_rc = OBJ_IRQ_USER;
		rockchip_pcie_raise_msi_irq(rockchip, PCIe_CLIENT_MSI_OBJ_IRQ);
		break;
	case PCIE_EP_GET_USER_INFO:
		msg.bar0_phys_addr = rockchip->ib_target_address[0];

		ret = copy_to_user(uarg, &msg, sizeof(msg));
		if (ret) {
			dev_err(rockchip->pci.dev, "failed to get elbi data\n");
			return -EFAULT;
		}
		break;
	case PCIE_EP_SET_MMAP_RESOURCE:
		ret = copy_from_user(&mmap_res, uarg, sizeof(mmap_res));
		if (ret) {
			dev_err(rockchip->pci.dev, "failed to get copy from\n");
			return -EFAULT;
		}

		if (mmap_res >= PCIE_EP_MMAP_RESOURCE_MAX) {
			dev_err(rockchip->pci.dev, "mmap index %d is out of number\n", mmap_res);
			return -EINVAL;
		}

		rockchip->cur_mmap_res = mmap_res;
		break;
	default:
		break;
	}
	return 0;
}

static int pcie_ep_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct rockchip_pcie *rockchip = (struct rockchip_pcie *)file->private_data;
	size_t size = vma->vm_end - vma->vm_start;
	int err;
	unsigned long addr;

	switch (rockchip->cur_mmap_res) {
	case PCIE_EP_MMAP_RESOURCE_DBI:
		if (size > PCIE_DBI_SIZE) {
			dev_warn(rockchip->pci.dev, "dbi mmap size is out of limitation\n");
			return -EINVAL;
		}
		addr = rockchip->dbi_base_physical;
		break;
	case PCIE_EP_MMAP_RESOURCE_BAR0:
		if (size > rockchip->ib_target_size[0]) {
			dev_warn(rockchip->pci.dev, "bar0 mmap size is out of limitation\n");
			return -EINVAL;
		}
		addr = rockchip->ib_target_address[0];
		break;
	case PCIE_EP_MMAP_RESOURCE_BAR2:
		if (size > rockchip->ib_target_size[2]) {
			dev_warn(rockchip->pci.dev, "bar2 mmap size is out of limitation\n");
			return -EINVAL;
		}
		addr = rockchip->ib_target_address[2];
		break;
	default:
		dev_err(rockchip->pci.dev, "cur mmap_res %d is unsurreport\n", rockchip->cur_mmap_res);
		return -EINVAL;
	}

	vma->vm_flags |= VM_IO;
	vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP);

	if (rockchip->cur_mmap_res == PCIE_EP_MMAP_RESOURCE_BAR2)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	err = remap_pfn_range(vma, vma->vm_start,
			      __phys_to_pfn(addr),
			      size, vma->vm_page_prot);
	if (err)
		return -EAGAIN;

	return 0;
}

static const struct file_operations pcie_ep_ops = {
	.owner = THIS_MODULE,
	.open = pcie_ep_open,
	.release = pcie_ep_release,
	.unlocked_ioctl = pcie_ep_ioctl,
	.fasync = pcie_ep_fasync,
	.mmap = pcie_ep_mmap,
};

static int rockchip_pcie_add_misc(struct rockchip_pcie *rockchip)
{
	int ret;
	struct rockchip_pcie_misc_dev *pcie_dev;

	pcie_dev = devm_kzalloc(rockchip->pci.dev, sizeof(struct rockchip_pcie_misc_dev),
				GFP_KERNEL);
	if (!pcie_dev)
		return -ENOMEM;

	pcie_dev->dev.minor = MISC_DYNAMIC_MINOR;
	pcie_dev->dev.name = "pcie_ep";
	pcie_dev->dev.fops = &pcie_ep_ops;
	pcie_dev->dev.parent = rockchip->pci.dev;

	ret = misc_register(&pcie_dev->dev);
	if (ret) {
		dev_err(rockchip->pci.dev, "pcie: failed to register misc device.\n");
		return ret;
	}

	pcie_dev->pcie = rockchip;

	dev_info(rockchip->pci.dev, "register misc device pcie_ep\n");

	return 0;
}

static int rockchip_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_pcie *rockchip;
	int ret;
	int retry, i;
	u32 reg;

	rockchip = devm_kzalloc(dev, sizeof(*rockchip), GFP_KERNEL);
	if (!rockchip)
		return -ENOMEM;

	platform_set_drvdata(pdev, rockchip);

	rockchip->pci.dev = dev;
	rockchip->pci.ops = &dw_pcie_ops;

	ret = rockchip_pcie_resource_get(pdev, rockchip);
	if (ret)
		return ret;

	/* DON'T MOVE ME: must be enable before phy init */
	rockchip->vpcie3v3 = devm_regulator_get_optional(dev, "vpcie3v3");
	if (IS_ERR(rockchip->vpcie3v3)) {
		if (PTR_ERR(rockchip->vpcie3v3) != -ENODEV)
			return PTR_ERR(rockchip->vpcie3v3);
		dev_info(dev, "no vpcie3v3 regulator found\n");
	}

	if (!IS_ERR(rockchip->vpcie3v3)) {
		ret = regulator_enable(rockchip->vpcie3v3);
		if (ret) {
			dev_err(dev, "fail to enable vpcie3v3 regulator\n");
			return ret;
		}
	}

	ret = rockchip_pcie_clk_init(rockchip);
	if (ret)
		goto disable_regulator;

	if (dw_pcie_link_up(&rockchip->pci)) {
		dev_info(dev, "already linkup\n");
		goto already_linkup;
	} else {
		dev_info(dev, "initial\n");
	}

	ret = rockchip_pcie_phy_init(rockchip);
	if (ret)
		goto deinit_clk;

	ret = rockchip_pcie_reset_control_release(rockchip);
	if (ret)
		goto deinit_phy;

	dw_pcie_setup(&rockchip->pci);

	dw_pcie_dbi_ro_wr_en(&rockchip->pci);
	rockchip_pcie_resize_bar(rockchip);
	rockchip_pcie_init_id(rockchip);
	dw_pcie_dbi_ro_wr_dis(&rockchip->pci);

	rockchip_pcie_fast_link_setup(rockchip);

	rockchip_pcie_start_link(&rockchip->pci);
	rockchip_pcie_devmode_update(rockchip, RKEP_MODE_KERNEL, RKEP_SMODE_LNKRDY);

	rockchip->hot_rst_wq = create_singlethread_workqueue("rkep_hot_rst_wq");
	if (!rockchip->hot_rst_wq) {
		dev_err(dev, "failed to create hot_rst workqueue\n");
		ret = -ENOMEM;
		goto deinit_phy;
	}
	INIT_WORK(&rockchip->hot_rst_work, rockchip_pcie_hot_rst_work);

	reg = rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_INTR_STATUS_MISC);
	if ((reg & BIT(2)) &&
	    (rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_HOT_RESET_CTRL) & PCIE_LTSSM_APP_DLY2_EN)) {
		rockchip_pcie_writel_apb(rockchip, PCIE_LTSSM_APP_DLY2_DONE | (PCIE_LTSSM_APP_DLY2_DONE << 16),
					 PCIE_CLIENT_HOT_RESET_CTRL);
		dev_info(dev, "hot reset ever\n");
	}
	rockchip_pcie_writel_apb(rockchip, reg, PCIE_CLIENT_INTR_STATUS_MISC);

	/* Enable client reset or link down interrupt */
	rockchip_pcie_writel_apb(rockchip, 0x40000, PCIE_CLIENT_INTR_MASK);

	for (retry = 0; retry < 10000; retry++) {
		if (dw_pcie_link_up(&rockchip->pci)) {
			/*
			 * We may be here in case of L0 in Gen1. But if EP is capable
			 * of Gen2 or Gen3, Gen switch may happen just in this time, but
			 * we keep on accessing devices in unstable link status. Given
			 * that LTSSM max timeout is 24ms per period, we can wait a bit
			 * more for Gen switch.
			 */
			msleep(2000);
			dev_info(dev, "PCIe Link up, LTSSM is 0x%x\n",
				 rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_LTSSM_STATUS));
			break;
		}

		dev_info_ratelimited(dev, "PCIe Linking... LTSSM is 0x%x\n",
				     rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_LTSSM_STATUS));
		msleep(20);
	}

	if (retry >= 10000) {
		ret = -ENODEV;
		goto deinit_phy;
	}

already_linkup:
	rockchip_pcie_devmode_update(rockchip, RKEP_MODE_KERNEL, RKEP_SMODE_LNKUP);
	rockchip->pci.iatu_unroll_enabled = rockchip_pcie_iatu_unroll_enabled(&rockchip->pci);
	for (i = 0; i < PCIE_BAR_MAX_NUM; i++)
		if (rockchip->ib_target_size[i])
			rockchip_pcie_ep_set_bar(rockchip, i, rockchip->ib_target_address[i]);

	ret = rockchip_pcie_init_dma_trx(rockchip);
	if (ret) {
		dev_err(dev, "failed to add dma extension\n");
		return ret;
	}

	if (rockchip->dma_obj) {
		rockchip->dma_obj->start_dma_func = rockchip_pcie_start_dma_dwc;
		rockchip->dma_obj->config_dma_func = rockchip_pcie_config_dma_dwc;
		rockchip->dma_obj->get_dma_status = rockchip_pcie_get_dma_status;
	}

	/* Enable client ELBI interrupt */
	rockchip_pcie_writel_apb(rockchip, 0x80000000, PCIE_CLIENT_INTR_MASK);
	/* Enable ELBI interrupt */
	rockchip_pcie_local_elbi_enable(rockchip);

	ret = rockchip_pcie_request_sys_irq(rockchip, pdev);
	if (ret)
		goto deinit_phy;

	rockchip_pcie_add_misc(rockchip);

	return 0;

deinit_phy:
	rockchip_pcie_phy_deinit(rockchip);
deinit_clk:
	clk_bulk_disable_unprepare(rockchip->clk_cnt, rockchip->clks);
disable_regulator:
	if (!IS_ERR(rockchip->vpcie3v3))
		regulator_disable(rockchip->vpcie3v3);

	return ret;
}

static struct platform_driver rk_plat_pcie_driver = {
	.driver = {
		.name	= "rk-pcie-ep",
		.of_match_table = rockchip_pcie_ep_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = rockchip_pcie_ep_probe,
};

module_platform_driver(rk_plat_pcie_driver);

MODULE_AUTHOR("Simon Xue <xxm@rock-chips.com>");
MODULE_DESCRIPTION("RockChip PCIe Controller EP driver");
MODULE_LICENSE("GPL");
