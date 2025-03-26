// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		https://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#include <linux/align.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma/edma.h>
#include <linux/gpio/consumer.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include "../../pci.h"
#include "pcie-designware.h"

static const char * const dw_pcie_app_clks[DW_PCIE_NUM_APP_CLKS] = {
	[DW_PCIE_DBI_CLK] = "dbi",
	[DW_PCIE_MSTR_CLK] = "mstr",
	[DW_PCIE_SLV_CLK] = "slv",
};

static const char * const dw_pcie_core_clks[DW_PCIE_NUM_CORE_CLKS] = {
	[DW_PCIE_PIPE_CLK] = "pipe",
	[DW_PCIE_CORE_CLK] = "core",
	[DW_PCIE_AUX_CLK] = "aux",
	[DW_PCIE_REF_CLK] = "ref",
};

static const char * const dw_pcie_app_rsts[DW_PCIE_NUM_APP_RSTS] = {
	[DW_PCIE_DBI_RST] = "dbi",
	[DW_PCIE_MSTR_RST] = "mstr",
	[DW_PCIE_SLV_RST] = "slv",
};

static const char * const dw_pcie_core_rsts[DW_PCIE_NUM_CORE_RSTS] = {
	[DW_PCIE_NON_STICKY_RST] = "non-sticky",
	[DW_PCIE_STICKY_RST] = "sticky",
	[DW_PCIE_CORE_RST] = "core",
	[DW_PCIE_PIPE_RST] = "pipe",
	[DW_PCIE_PHY_RST] = "phy",
	[DW_PCIE_HOT_RST] = "hot",
	[DW_PCIE_PWR_RST] = "pwr",
};

static int dw_pcie_get_clocks(struct dw_pcie *pci)
{
	int i, ret;

	for (i = 0; i < DW_PCIE_NUM_APP_CLKS; i++)
		pci->app_clks[i].id = dw_pcie_app_clks[i];

	for (i = 0; i < DW_PCIE_NUM_CORE_CLKS; i++)
		pci->core_clks[i].id = dw_pcie_core_clks[i];

	ret = devm_clk_bulk_get_optional(pci->dev, DW_PCIE_NUM_APP_CLKS,
					 pci->app_clks);
	if (ret)
		return ret;

	return devm_clk_bulk_get_optional(pci->dev, DW_PCIE_NUM_CORE_CLKS,
					  pci->core_clks);
}

static int dw_pcie_get_resets(struct dw_pcie *pci)
{
	int i, ret;

	for (i = 0; i < DW_PCIE_NUM_APP_RSTS; i++)
		pci->app_rsts[i].id = dw_pcie_app_rsts[i];

	for (i = 0; i < DW_PCIE_NUM_CORE_RSTS; i++)
		pci->core_rsts[i].id = dw_pcie_core_rsts[i];

	ret = devm_reset_control_bulk_get_optional_shared(pci->dev,
							  DW_PCIE_NUM_APP_RSTS,
							  pci->app_rsts);
	if (ret)
		return ret;

	ret = devm_reset_control_bulk_get_optional_exclusive(pci->dev,
							     DW_PCIE_NUM_CORE_RSTS,
							     pci->core_rsts);
	if (ret)
		return ret;

	pci->pe_rst = devm_gpiod_get_optional(pci->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(pci->pe_rst))
		return PTR_ERR(pci->pe_rst);

	return 0;
}

int dw_pcie_get_resources(struct dw_pcie *pci)
{
	struct platform_device *pdev = to_platform_device(pci->dev);
	struct device_node *np = dev_of_node(pci->dev);
	struct resource *res;
	int ret;

	if (!pci->dbi_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
		pci->dbi_base = devm_pci_remap_cfg_resource(pci->dev, res);
		if (IS_ERR(pci->dbi_base))
			return PTR_ERR(pci->dbi_base);
		pci->dbi_phys_addr = res->start;
	}

	/* DBI2 is mainly useful for the endpoint controller */
	if (!pci->dbi_base2) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi2");
		if (res) {
			pci->dbi_base2 = devm_pci_remap_cfg_resource(pci->dev, res);
			if (IS_ERR(pci->dbi_base2))
				return PTR_ERR(pci->dbi_base2);
		} else {
			pci->dbi_base2 = pci->dbi_base + SZ_4K;
		}
	}

	/* For non-unrolled iATU/eDMA platforms this range will be ignored */
	if (!pci->atu_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "atu");
		if (res) {
			pci->atu_size = resource_size(res);
			pci->atu_base = devm_ioremap_resource(pci->dev, res);
			if (IS_ERR(pci->atu_base))
				return PTR_ERR(pci->atu_base);
			pci->atu_phys_addr = res->start;
		} else {
			pci->atu_base = pci->dbi_base + DEFAULT_DBI_ATU_OFFSET;
		}
	}

	/* Set a default value suitable for at most 8 in and 8 out windows */
	if (!pci->atu_size)
		pci->atu_size = SZ_4K;

	/* eDMA region can be mapped to a custom base address */
	if (!pci->edma.reg_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dma");
		if (res) {
			pci->edma.reg_base = devm_ioremap_resource(pci->dev, res);
			if (IS_ERR(pci->edma.reg_base))
				return PTR_ERR(pci->edma.reg_base);
		} else if (pci->atu_size >= 2 * DEFAULT_DBI_DMA_OFFSET) {
			pci->edma.reg_base = pci->atu_base + DEFAULT_DBI_DMA_OFFSET;
		}
	}

	/* LLDD is supposed to manually switch the clocks and resets state */
	if (dw_pcie_cap_is(pci, REQ_RES)) {
		ret = dw_pcie_get_clocks(pci);
		if (ret)
			return ret;

		ret = dw_pcie_get_resets(pci);
		if (ret)
			return ret;
	}

	if (pci->max_link_speed < 1)
		pci->max_link_speed = of_pci_get_max_link_speed(np);

	of_property_read_u32(np, "num-lanes", &pci->num_lanes);

	if (of_property_read_bool(np, "snps,enable-cdm-check"))
		dw_pcie_cap_set(pci, CDM_CHECK);

	return 0;
}

void dw_pcie_version_detect(struct dw_pcie *pci)
{
	u32 ver;

	/* The content of the CSR is zero on DWC PCIe older than v4.70a */
	ver = dw_pcie_readl_dbi(pci, PCIE_VERSION_NUMBER);
	if (!ver)
		return;

	if (pci->version && pci->version != ver)
		dev_warn(pci->dev, "Versions don't match (%08x != %08x)\n",
			 pci->version, ver);
	else
		pci->version = ver;

	ver = dw_pcie_readl_dbi(pci, PCIE_VERSION_TYPE);

	if (pci->type && pci->type != ver)
		dev_warn(pci->dev, "Types don't match (%08x != %08x)\n",
			 pci->type, ver);
	else
		pci->type = ver;
}

/*
 * These interfaces resemble the pci_find_*capability() interfaces, but these
 * are for configuring host controllers, which are bridges *to* PCI devices but
 * are not PCI devices themselves.
 */
static u8 __dw_pcie_find_next_cap(struct dw_pcie *pci, u8 cap_ptr,
				  u8 cap)
{
	u8 cap_id, next_cap_ptr;
	u16 reg;

	if (!cap_ptr)
		return 0;

	reg = dw_pcie_readw_dbi(pci, cap_ptr);
	cap_id = (reg & 0x00ff);

	if (cap_id > PCI_CAP_ID_MAX)
		return 0;

	if (cap_id == cap)
		return cap_ptr;

	next_cap_ptr = (reg & 0xff00) >> 8;
	return __dw_pcie_find_next_cap(pci, next_cap_ptr, cap);
}

u8 dw_pcie_find_capability(struct dw_pcie *pci, u8 cap)
{
	u8 next_cap_ptr;
	u16 reg;

	reg = dw_pcie_readw_dbi(pci, PCI_CAPABILITY_LIST);
	next_cap_ptr = (reg & 0x00ff);

	return __dw_pcie_find_next_cap(pci, next_cap_ptr, cap);
}
EXPORT_SYMBOL_GPL(dw_pcie_find_capability);

static u16 dw_pcie_find_next_ext_capability(struct dw_pcie *pci, u16 start,
					    u8 cap)
{
	u32 header;
	int ttl;
	int pos = PCI_CFG_SPACE_SIZE;

	/* minimum 8 bytes per capability */
	ttl = (PCI_CFG_SPACE_EXP_SIZE - PCI_CFG_SPACE_SIZE) / 8;

	if (start)
		pos = start;

	header = dw_pcie_readl_dbi(pci, pos);
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

		header = dw_pcie_readl_dbi(pci, pos);
	}

	return 0;
}

u16 dw_pcie_find_ext_capability(struct dw_pcie *pci, u8 cap)
{
	return dw_pcie_find_next_ext_capability(pci, 0, cap);
}
EXPORT_SYMBOL_GPL(dw_pcie_find_ext_capability);

int dw_pcie_read(void __iomem *addr, int size, u32 *val)
{
	if (!IS_ALIGNED((uintptr_t)addr, size)) {
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
EXPORT_SYMBOL_GPL(dw_pcie_read);

int dw_pcie_write(void __iomem *addr, int size, u32 val)
{
	if (!IS_ALIGNED((uintptr_t)addr, size))
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
EXPORT_SYMBOL_GPL(dw_pcie_write);

u32 dw_pcie_read_dbi(struct dw_pcie *pci, u32 reg, size_t size)
{
	int ret;
	u32 val;

	if (pci->ops && pci->ops->read_dbi)
		return pci->ops->read_dbi(pci, pci->dbi_base, reg, size);

	ret = dw_pcie_read(pci->dbi_base + reg, size, &val);
	if (ret)
		dev_err(pci->dev, "Read DBI address failed\n");

	return val;
}
EXPORT_SYMBOL_GPL(dw_pcie_read_dbi);

void dw_pcie_write_dbi(struct dw_pcie *pci, u32 reg, size_t size, u32 val)
{
	int ret;

	if (pci->ops && pci->ops->write_dbi) {
		pci->ops->write_dbi(pci, pci->dbi_base, reg, size, val);
		return;
	}

	ret = dw_pcie_write(pci->dbi_base + reg, size, val);
	if (ret)
		dev_err(pci->dev, "Write DBI address failed\n");
}
EXPORT_SYMBOL_GPL(dw_pcie_write_dbi);

void dw_pcie_write_dbi2(struct dw_pcie *pci, u32 reg, size_t size, u32 val)
{
	int ret;

	if (pci->ops && pci->ops->write_dbi2) {
		pci->ops->write_dbi2(pci, pci->dbi_base2, reg, size, val);
		return;
	}

	ret = dw_pcie_write(pci->dbi_base2 + reg, size, val);
	if (ret)
		dev_err(pci->dev, "write DBI address failed\n");
}
EXPORT_SYMBOL_GPL(dw_pcie_write_dbi2);

static inline void __iomem *dw_pcie_select_atu(struct dw_pcie *pci, u32 dir,
					       u32 index)
{
	if (dw_pcie_cap_is(pci, IATU_UNROLL))
		return pci->atu_base + PCIE_ATU_UNROLL_BASE(dir, index);

	dw_pcie_writel_dbi(pci, PCIE_ATU_VIEWPORT, dir | index);
	return pci->atu_base;
}

static u32 dw_pcie_readl_atu(struct dw_pcie *pci, u32 dir, u32 index, u32 reg)
{
	void __iomem *base;
	int ret;
	u32 val;

	base = dw_pcie_select_atu(pci, dir, index);

	if (pci->ops && pci->ops->read_dbi)
		return pci->ops->read_dbi(pci, base, reg, 4);

	ret = dw_pcie_read(base + reg, 4, &val);
	if (ret)
		dev_err(pci->dev, "Read ATU address failed\n");

	return val;
}

static void dw_pcie_writel_atu(struct dw_pcie *pci, u32 dir, u32 index,
			       u32 reg, u32 val)
{
	void __iomem *base;
	int ret;

	base = dw_pcie_select_atu(pci, dir, index);

	if (pci->ops && pci->ops->write_dbi) {
		pci->ops->write_dbi(pci, base, reg, 4, val);
		return;
	}

	ret = dw_pcie_write(base + reg, 4, val);
	if (ret)
		dev_err(pci->dev, "Write ATU address failed\n");
}

static inline u32 dw_pcie_readl_atu_ob(struct dw_pcie *pci, u32 index, u32 reg)
{
	return dw_pcie_readl_atu(pci, PCIE_ATU_REGION_DIR_OB, index, reg);
}

static inline void dw_pcie_writel_atu_ob(struct dw_pcie *pci, u32 index, u32 reg,
					 u32 val)
{
	dw_pcie_writel_atu(pci, PCIE_ATU_REGION_DIR_OB, index, reg, val);
}

static inline u32 dw_pcie_enable_ecrc(u32 val)
{
	/*
	 * DesignWare core version 4.90A has a design issue where the 'TD'
	 * bit in the Control register-1 of the ATU outbound region acts
	 * like an override for the ECRC setting, i.e., the presence of TLP
	 * Digest (ECRC) in the outgoing TLPs is solely determined by this
	 * bit. This is contrary to the PCIe spec which says that the
	 * enablement of the ECRC is solely determined by the AER
	 * registers.
	 *
	 * Because of this, even when the ECRC is enabled through AER
	 * registers, the transactions going through ATU won't have TLP
	 * Digest as there is no way the PCI core AER code could program
	 * the TD bit which is specific to the DesignWare core.
	 *
	 * The best way to handle this scenario is to program the TD bit
	 * always. It affects only the traffic from root port to downstream
	 * devices.
	 *
	 * At this point,
	 * When ECRC is enabled in AER registers, everything works normally
	 * When ECRC is NOT enabled in AER registers, then,
	 * on Root Port:- TLP Digest (DWord size) gets appended to each packet
	 *                even through it is not required. Since downstream
	 *                TLPs are mostly for configuration accesses and BAR
	 *                accesses, they are not in critical path and won't
	 *                have much negative effect on the performance.
	 * on End Point:- TLP Digest is received for some/all the packets coming
	 *                from the root port. TLP Digest is ignored because,
	 *                as per the PCIe Spec r5.0 v1.0 section 2.2.3
	 *                "TLP Digest Rules", when an endpoint receives TLP
	 *                Digest when its ECRC check functionality is disabled
	 *                in AER registers, received TLP Digest is just ignored.
	 * Since there is no issue or error reported either side, best way to
	 * handle the scenario is to program TD bit by default.
	 */

	return val | PCIE_ATU_TD;
}

int dw_pcie_prog_outbound_atu(struct dw_pcie *pci,
			      const struct dw_pcie_ob_atu_cfg *atu)
{
	u64 cpu_addr = atu->cpu_addr;
	u32 retries, val;
	u64 limit_addr;

	if (pci->ops && pci->ops->cpu_addr_fixup)
		cpu_addr = pci->ops->cpu_addr_fixup(pci, cpu_addr);

	limit_addr = cpu_addr + atu->size - 1;

	if ((limit_addr & ~pci->region_limit) != (cpu_addr & ~pci->region_limit) ||
	    !IS_ALIGNED(cpu_addr, pci->region_align) ||
	    !IS_ALIGNED(atu->pci_addr, pci->region_align) || !atu->size) {
		return -EINVAL;
	}

	dw_pcie_writel_atu_ob(pci, atu->index, PCIE_ATU_LOWER_BASE,
			      lower_32_bits(cpu_addr));
	dw_pcie_writel_atu_ob(pci, atu->index, PCIE_ATU_UPPER_BASE,
			      upper_32_bits(cpu_addr));

	dw_pcie_writel_atu_ob(pci, atu->index, PCIE_ATU_LIMIT,
			      lower_32_bits(limit_addr));
	if (dw_pcie_ver_is_ge(pci, 460A))
		dw_pcie_writel_atu_ob(pci, atu->index, PCIE_ATU_UPPER_LIMIT,
				      upper_32_bits(limit_addr));

	dw_pcie_writel_atu_ob(pci, atu->index, PCIE_ATU_LOWER_TARGET,
			      lower_32_bits(atu->pci_addr));
	dw_pcie_writel_atu_ob(pci, atu->index, PCIE_ATU_UPPER_TARGET,
			      upper_32_bits(atu->pci_addr));

	val = atu->type | atu->routing | PCIE_ATU_FUNC_NUM(atu->func_no);
	if (upper_32_bits(limit_addr) > upper_32_bits(cpu_addr) &&
	    dw_pcie_ver_is_ge(pci, 460A))
		val |= PCIE_ATU_INCREASE_REGION_SIZE;
	if (dw_pcie_ver_is(pci, 490A))
		val = dw_pcie_enable_ecrc(val);
	dw_pcie_writel_atu_ob(pci, atu->index, PCIE_ATU_REGION_CTRL1, val);

	val = PCIE_ATU_ENABLE;
	if (atu->type == PCIE_ATU_TYPE_MSG) {
		/* The data-less messages only for now */
		val |= PCIE_ATU_INHIBIT_PAYLOAD | atu->code;
	}
	dw_pcie_writel_atu_ob(pci, atu->index, PCIE_ATU_REGION_CTRL2, val);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie_readl_atu_ob(pci, atu->index, PCIE_ATU_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pci->dev, "Outbound iATU is not being enabled\n");

	return -ETIMEDOUT;
}

static inline u32 dw_pcie_readl_atu_ib(struct dw_pcie *pci, u32 index, u32 reg)
{
	return dw_pcie_readl_atu(pci, PCIE_ATU_REGION_DIR_IB, index, reg);
}

static inline void dw_pcie_writel_atu_ib(struct dw_pcie *pci, u32 index, u32 reg,
					 u32 val)
{
	dw_pcie_writel_atu(pci, PCIE_ATU_REGION_DIR_IB, index, reg, val);
}

int dw_pcie_prog_inbound_atu(struct dw_pcie *pci, int index, int type,
			     u64 cpu_addr, u64 pci_addr, u64 size)
{
	u64 limit_addr = pci_addr + size - 1;
	u32 retries, val;

	if ((limit_addr & ~pci->region_limit) != (pci_addr & ~pci->region_limit) ||
	    !IS_ALIGNED(cpu_addr, pci->region_align) ||
	    !IS_ALIGNED(pci_addr, pci->region_align) || !size) {
		return -EINVAL;
	}

	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_LOWER_BASE,
			      lower_32_bits(pci_addr));
	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_UPPER_BASE,
			      upper_32_bits(pci_addr));

	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_LIMIT,
			      lower_32_bits(limit_addr));
	if (dw_pcie_ver_is_ge(pci, 460A))
		dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_UPPER_LIMIT,
				      upper_32_bits(limit_addr));

	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_LOWER_TARGET,
			      lower_32_bits(cpu_addr));
	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_UPPER_TARGET,
			      upper_32_bits(cpu_addr));

	val = type;
	if (upper_32_bits(limit_addr) > upper_32_bits(pci_addr) &&
	    dw_pcie_ver_is_ge(pci, 460A))
		val |= PCIE_ATU_INCREASE_REGION_SIZE;
	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_REGION_CTRL1, val);
	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_REGION_CTRL2, PCIE_ATU_ENABLE);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie_readl_atu_ib(pci, index, PCIE_ATU_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pci->dev, "Inbound iATU is not being enabled\n");

	return -ETIMEDOUT;
}

int dw_pcie_prog_ep_inbound_atu(struct dw_pcie *pci, u8 func_no, int index,
				int type, u64 cpu_addr, u8 bar, size_t size)
{
	u32 retries, val;

	if (!IS_ALIGNED(cpu_addr, pci->region_align) ||
	    !IS_ALIGNED(cpu_addr, size))
		return -EINVAL;

	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_LOWER_TARGET,
			      lower_32_bits(cpu_addr));
	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_UPPER_TARGET,
			      upper_32_bits(cpu_addr));

	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_REGION_CTRL1, type |
			      PCIE_ATU_FUNC_NUM(func_no));
	dw_pcie_writel_atu_ib(pci, index, PCIE_ATU_REGION_CTRL2,
			      PCIE_ATU_ENABLE | PCIE_ATU_FUNC_NUM_MATCH_EN |
			      PCIE_ATU_BAR_MODE_ENABLE | (bar << 8));

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie_readl_atu_ib(pci, index, PCIE_ATU_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}

	dev_err(pci->dev, "Inbound iATU is not being enabled\n");

	return -ETIMEDOUT;
}

void dw_pcie_disable_atu(struct dw_pcie *pci, u32 dir, int index)
{
	dw_pcie_writel_atu(pci, dir, index, PCIE_ATU_REGION_CTRL2, 0);
}

int dw_pcie_wait_for_link(struct dw_pcie *pci)
{
	u32 offset, val;
	int retries;

	/* Check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (dw_pcie_link_up(pci))
			break;

		msleep(LINK_WAIT_SLEEP_MS);
	}

	if (retries >= LINK_WAIT_MAX_RETRIES) {
		dev_info(pci->dev, "Phy link never came up\n");
		return -ETIMEDOUT;
	}

	offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	val = dw_pcie_readw_dbi(pci, offset + PCI_EXP_LNKSTA);

	dev_info(pci->dev, "PCIe Gen.%u x%u link up\n",
		 FIELD_GET(PCI_EXP_LNKSTA_CLS, val),
		 FIELD_GET(PCI_EXP_LNKSTA_NLW, val));

	return 0;
}
EXPORT_SYMBOL_GPL(dw_pcie_wait_for_link);

int dw_pcie_link_up(struct dw_pcie *pci)
{
	u32 val;

	if (pci->ops && pci->ops->link_up)
		return pci->ops->link_up(pci);

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_DEBUG1);
	return ((val & PCIE_PORT_DEBUG1_LINK_UP) &&
		(!(val & PCIE_PORT_DEBUG1_LINK_IN_TRAINING)));
}
EXPORT_SYMBOL_GPL(dw_pcie_link_up);

void dw_pcie_upconfig_setup(struct dw_pcie *pci)
{
	u32 val;

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL);
	val |= PORT_MLTI_UPCFG_SUPPORT;
	dw_pcie_writel_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, val);
}
EXPORT_SYMBOL_GPL(dw_pcie_upconfig_setup);

static void dw_pcie_link_set_max_speed(struct dw_pcie *pci)
{
	u32 cap, ctrl2, link_speed;
	u8 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);

	cap = dw_pcie_readl_dbi(pci, offset + PCI_EXP_LNKCAP);

	/*
	 * Even if the platform doesn't want to limit the maximum link speed,
	 * just cache the hardware default value so that the vendor drivers can
	 * use it to do any link specific configuration.
	 */
	if (pci->max_link_speed < 1) {
		pci->max_link_speed = FIELD_GET(PCI_EXP_LNKCAP_SLS, cap);
		return;
	}

	ctrl2 = dw_pcie_readl_dbi(pci, offset + PCI_EXP_LNKCTL2);
	ctrl2 &= ~PCI_EXP_LNKCTL2_TLS;

	switch (pcie_link_speed[pci->max_link_speed]) {
	case PCIE_SPEED_2_5GT:
		link_speed = PCI_EXP_LNKCTL2_TLS_2_5GT;
		break;
	case PCIE_SPEED_5_0GT:
		link_speed = PCI_EXP_LNKCTL2_TLS_5_0GT;
		break;
	case PCIE_SPEED_8_0GT:
		link_speed = PCI_EXP_LNKCTL2_TLS_8_0GT;
		break;
	case PCIE_SPEED_16_0GT:
		link_speed = PCI_EXP_LNKCTL2_TLS_16_0GT;
		break;
	default:
		/* Use hardware capability */
		link_speed = FIELD_GET(PCI_EXP_LNKCAP_SLS, cap);
		ctrl2 &= ~PCI_EXP_LNKCTL2_HASD;
		break;
	}

	dw_pcie_writel_dbi(pci, offset + PCI_EXP_LNKCTL2, ctrl2 | link_speed);

	cap &= ~((u32)PCI_EXP_LNKCAP_SLS);
	dw_pcie_writel_dbi(pci, offset + PCI_EXP_LNKCAP, cap | link_speed);

}

static void dw_pcie_link_set_max_link_width(struct dw_pcie *pci, u32 num_lanes)
{
	u32 lnkcap, lwsc, plc;
	u8 cap;

	if (!num_lanes)
		return;

	/* Set the number of lanes */
	plc = dw_pcie_readl_dbi(pci, PCIE_PORT_LINK_CONTROL);
	plc &= ~PORT_LINK_FAST_LINK_MODE;
	plc &= ~PORT_LINK_MODE_MASK;

	/* Set link width speed control register */
	lwsc = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	lwsc &= ~PORT_LOGIC_LINK_WIDTH_MASK;
	switch (num_lanes) {
	case 1:
		plc |= PORT_LINK_MODE_1_LANES;
		lwsc |= PORT_LOGIC_LINK_WIDTH_1_LANES;
		break;
	case 2:
		plc |= PORT_LINK_MODE_2_LANES;
		lwsc |= PORT_LOGIC_LINK_WIDTH_2_LANES;
		break;
	case 4:
		plc |= PORT_LINK_MODE_4_LANES;
		lwsc |= PORT_LOGIC_LINK_WIDTH_4_LANES;
		break;
	case 8:
		plc |= PORT_LINK_MODE_8_LANES;
		lwsc |= PORT_LOGIC_LINK_WIDTH_8_LANES;
		break;
	default:
		dev_err(pci->dev, "num-lanes %u: invalid value\n", num_lanes);
		return;
	}
	dw_pcie_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, plc);
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, lwsc);

	cap = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	lnkcap = dw_pcie_readl_dbi(pci, cap + PCI_EXP_LNKCAP);
	lnkcap &= ~PCI_EXP_LNKCAP_MLW;
	lnkcap |= FIELD_PREP(PCI_EXP_LNKCAP_MLW, num_lanes);
	dw_pcie_writel_dbi(pci, cap + PCI_EXP_LNKCAP, lnkcap);
}

void dw_pcie_iatu_detect(struct dw_pcie *pci)
{
	int max_region, ob, ib;
	u32 val, min, dir;
	u64 max;

	val = dw_pcie_readl_dbi(pci, PCIE_ATU_VIEWPORT);
	if (val == 0xFFFFFFFF) {
		dw_pcie_cap_set(pci, IATU_UNROLL);

		max_region = min((int)pci->atu_size / 512, 256);
	} else {
		pci->atu_base = pci->dbi_base + PCIE_ATU_VIEWPORT_BASE;
		pci->atu_size = PCIE_ATU_VIEWPORT_SIZE;

		dw_pcie_writel_dbi(pci, PCIE_ATU_VIEWPORT, 0xFF);
		max_region = dw_pcie_readl_dbi(pci, PCIE_ATU_VIEWPORT) + 1;
	}

	for (ob = 0; ob < max_region; ob++) {
		dw_pcie_writel_atu_ob(pci, ob, PCIE_ATU_LOWER_TARGET, 0x11110000);
		val = dw_pcie_readl_atu_ob(pci, ob, PCIE_ATU_LOWER_TARGET);
		if (val != 0x11110000)
			break;
	}

	for (ib = 0; ib < max_region; ib++) {
		dw_pcie_writel_atu_ib(pci, ib, PCIE_ATU_LOWER_TARGET, 0x11110000);
		val = dw_pcie_readl_atu_ib(pci, ib, PCIE_ATU_LOWER_TARGET);
		if (val != 0x11110000)
			break;
	}

	if (ob) {
		dir = PCIE_ATU_REGION_DIR_OB;
	} else if (ib) {
		dir = PCIE_ATU_REGION_DIR_IB;
	} else {
		dev_err(pci->dev, "No iATU regions found\n");
		return;
	}

	dw_pcie_writel_atu(pci, dir, 0, PCIE_ATU_LIMIT, 0x0);
	min = dw_pcie_readl_atu(pci, dir, 0, PCIE_ATU_LIMIT);

	if (dw_pcie_ver_is_ge(pci, 460A)) {
		dw_pcie_writel_atu(pci, dir, 0, PCIE_ATU_UPPER_LIMIT, 0xFFFFFFFF);
		max = dw_pcie_readl_atu(pci, dir, 0, PCIE_ATU_UPPER_LIMIT);
	} else {
		max = 0;
	}

	pci->num_ob_windows = ob;
	pci->num_ib_windows = ib;
	pci->region_align = 1 << fls(min);
	pci->region_limit = (max << 32) | (SZ_4G - 1);

	dev_info(pci->dev, "iATU: unroll %s, %u ob, %u ib, align %uK, limit %lluG\n",
		 dw_pcie_cap_is(pci, IATU_UNROLL) ? "T" : "F",
		 pci->num_ob_windows, pci->num_ib_windows,
		 pci->region_align / SZ_1K, (pci->region_limit + 1) / SZ_1G);
}

static u32 dw_pcie_readl_dma(struct dw_pcie *pci, u32 reg)
{
	u32 val = 0;
	int ret;

	if (pci->ops && pci->ops->read_dbi)
		return pci->ops->read_dbi(pci, pci->edma.reg_base, reg, 4);

	ret = dw_pcie_read(pci->edma.reg_base + reg, 4, &val);
	if (ret)
		dev_err(pci->dev, "Read DMA address failed\n");

	return val;
}

static int dw_pcie_edma_irq_vector(struct device *dev, unsigned int nr)
{
	struct platform_device *pdev = to_platform_device(dev);
	char name[6];
	int ret;

	if (nr >= EDMA_MAX_WR_CH + EDMA_MAX_RD_CH)
		return -EINVAL;

	ret = platform_get_irq_byname_optional(pdev, "dma");
	if (ret > 0)
		return ret;

	snprintf(name, sizeof(name), "dma%u", nr);

	return platform_get_irq_byname_optional(pdev, name);
}

static struct dw_edma_plat_ops dw_pcie_edma_ops = {
	.irq_vector = dw_pcie_edma_irq_vector,
};

static void dw_pcie_edma_init_data(struct dw_pcie *pci)
{
	pci->edma.dev = pci->dev;

	if (!pci->edma.ops)
		pci->edma.ops = &dw_pcie_edma_ops;

	pci->edma.flags |= DW_EDMA_CHIP_LOCAL;
}

static int dw_pcie_edma_find_mf(struct dw_pcie *pci)
{
	u32 val;

	/*
	 * Bail out finding the mapping format if it is already set by the glue
	 * driver. Also ensure that the edma.reg_base is pointing to a valid
	 * memory region.
	 */
	if (pci->edma.mf != EDMA_MF_EDMA_LEGACY)
		return pci->edma.reg_base ? 0 : -ENODEV;

	/*
	 * Indirect eDMA CSRs access has been completely removed since v5.40a
	 * thus no space is now reserved for the eDMA channels viewport and
	 * former DMA CTRL register is no longer fixed to FFs.
	 */
	if (dw_pcie_ver_is_ge(pci, 540A))
		val = 0xFFFFFFFF;
	else
		val = dw_pcie_readl_dbi(pci, PCIE_DMA_VIEWPORT_BASE + PCIE_DMA_CTRL);

	if (val == 0xFFFFFFFF && pci->edma.reg_base) {
		pci->edma.mf = EDMA_MF_EDMA_UNROLL;
	} else if (val != 0xFFFFFFFF) {
		pci->edma.mf = EDMA_MF_EDMA_LEGACY;

		pci->edma.reg_base = pci->dbi_base + PCIE_DMA_VIEWPORT_BASE;
	} else {
		return -ENODEV;
	}

	return 0;
}

static int dw_pcie_edma_find_channels(struct dw_pcie *pci)
{
	u32 val;

	/*
	 * Autodetect the read/write channels count only for non-HDMA platforms.
	 * HDMA platforms with native CSR mapping doesn't support autodetect,
	 * so the glue drivers should've passed the valid count already. If not,
	 * the below sanity check will catch it.
	 */
	if (pci->edma.mf != EDMA_MF_HDMA_NATIVE) {
		val = dw_pcie_readl_dma(pci, PCIE_DMA_CTRL);

		pci->edma.ll_wr_cnt = FIELD_GET(PCIE_DMA_NUM_WR_CHAN, val);
		pci->edma.ll_rd_cnt = FIELD_GET(PCIE_DMA_NUM_RD_CHAN, val);
	}

	/* Sanity check the channels count if the mapping was incorrect */
	if (!pci->edma.ll_wr_cnt || pci->edma.ll_wr_cnt > EDMA_MAX_WR_CH ||
	    !pci->edma.ll_rd_cnt || pci->edma.ll_rd_cnt > EDMA_MAX_RD_CH)
		return -EINVAL;

	return 0;
}

static int dw_pcie_edma_find_chip(struct dw_pcie *pci)
{
	int ret;

	dw_pcie_edma_init_data(pci);

	ret = dw_pcie_edma_find_mf(pci);
	if (ret)
		return ret;

	return dw_pcie_edma_find_channels(pci);
}

static int dw_pcie_edma_irq_verify(struct dw_pcie *pci)
{
	struct platform_device *pdev = to_platform_device(pci->dev);
	u16 ch_cnt = pci->edma.ll_wr_cnt + pci->edma.ll_rd_cnt;
	char name[15];
	int ret;

	if (pci->edma.nr_irqs == 1)
		return 0;
	else if (pci->edma.nr_irqs > 1)
		return pci->edma.nr_irqs != ch_cnt ? -EINVAL : 0;

	ret = platform_get_irq_byname_optional(pdev, "dma");
	if (ret > 0) {
		pci->edma.nr_irqs = 1;
		return 0;
	}

	for (; pci->edma.nr_irqs < ch_cnt; pci->edma.nr_irqs++) {
		snprintf(name, sizeof(name), "dma%d", pci->edma.nr_irqs);

		ret = platform_get_irq_byname_optional(pdev, name);
		if (ret <= 0)
			return -EINVAL;
	}

	return 0;
}

static int dw_pcie_edma_ll_alloc(struct dw_pcie *pci)
{
	struct dw_edma_region *ll;
	dma_addr_t paddr;
	int i;

	for (i = 0; i < pci->edma.ll_wr_cnt; i++) {
		ll = &pci->edma.ll_region_wr[i];
		ll->sz = DMA_LLP_MEM_SIZE;
		ll->vaddr.mem = dmam_alloc_coherent(pci->dev, ll->sz,
						    &paddr, GFP_KERNEL);
		if (!ll->vaddr.mem)
			return -ENOMEM;

		ll->paddr = paddr;
	}

	for (i = 0; i < pci->edma.ll_rd_cnt; i++) {
		ll = &pci->edma.ll_region_rd[i];
		ll->sz = DMA_LLP_MEM_SIZE;
		ll->vaddr.mem = dmam_alloc_coherent(pci->dev, ll->sz,
						    &paddr, GFP_KERNEL);
		if (!ll->vaddr.mem)
			return -ENOMEM;

		ll->paddr = paddr;
	}

	return 0;
}

int dw_pcie_edma_detect(struct dw_pcie *pci)
{
	int ret;

	/* Don't fail if no eDMA was found (for the backward compatibility) */
	ret = dw_pcie_edma_find_chip(pci);
	if (ret)
		return 0;

	/* Don't fail on the IRQs verification (for the backward compatibility) */
	ret = dw_pcie_edma_irq_verify(pci);
	if (ret) {
		dev_err(pci->dev, "Invalid eDMA IRQs found\n");
		return 0;
	}

	ret = dw_pcie_edma_ll_alloc(pci);
	if (ret) {
		dev_err(pci->dev, "Couldn't allocate LLP memory\n");
		return ret;
	}

	/* Don't fail if the DW eDMA driver can't find the device */
	ret = dw_edma_probe(&pci->edma);
	if (ret && ret != -ENODEV) {
		dev_err(pci->dev, "Couldn't register eDMA device\n");
		return ret;
	}

	dev_info(pci->dev, "eDMA: unroll %s, %hu wr, %hu rd\n",
		 pci->edma.mf == EDMA_MF_EDMA_UNROLL ? "T" : "F",
		 pci->edma.ll_wr_cnt, pci->edma.ll_rd_cnt);

	return 0;
}

void dw_pcie_edma_remove(struct dw_pcie *pci)
{
	dw_edma_remove(&pci->edma);
}

void dw_pcie_setup(struct dw_pcie *pci)
{
	u32 val;

	dw_pcie_link_set_max_speed(pci);

	/* Configure Gen1 N_FTS */
	if (pci->n_fts[0]) {
		val = dw_pcie_readl_dbi(pci, PCIE_PORT_AFR);
		val &= ~(PORT_AFR_N_FTS_MASK | PORT_AFR_CC_N_FTS_MASK);
		val |= PORT_AFR_N_FTS(pci->n_fts[0]);
		val |= PORT_AFR_CC_N_FTS(pci->n_fts[0]);
		dw_pcie_writel_dbi(pci, PCIE_PORT_AFR, val);
	}

	/* Configure Gen2+ N_FTS */
	if (pci->n_fts[1]) {
		val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
		val &= ~PORT_LOGIC_N_FTS_MASK;
		val |= pci->n_fts[1];
		dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);
	}

	if (dw_pcie_cap_is(pci, CDM_CHECK)) {
		val = dw_pcie_readl_dbi(pci, PCIE_PL_CHK_REG_CONTROL_STATUS);
		val |= PCIE_PL_CHK_REG_CHK_REG_CONTINUOUS |
		       PCIE_PL_CHK_REG_CHK_REG_START;
		dw_pcie_writel_dbi(pci, PCIE_PL_CHK_REG_CONTROL_STATUS, val);
	}

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_LINK_CONTROL);
	val &= ~PORT_LINK_FAST_LINK_MODE;
	val |= PORT_LINK_DLL_LINK_EN;
	dw_pcie_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, val);

	dw_pcie_link_set_max_link_width(pci, pci->num_lanes);
}
