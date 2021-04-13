// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		https://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/types.h>

#include "../../pci.h"
#include "pcie-designware.h"

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

static u32 dw_pcie_readl_atu(struct dw_pcie *pci, u32 reg)
{
	int ret;
	u32 val;

	if (pci->ops && pci->ops->read_dbi)
		return pci->ops->read_dbi(pci, pci->atu_base, reg, 4);

	ret = dw_pcie_read(pci->atu_base + reg, 4, &val);
	if (ret)
		dev_err(pci->dev, "Read ATU address failed\n");

	return val;
}

static void dw_pcie_writel_atu(struct dw_pcie *pci, u32 reg, u32 val)
{
	int ret;

	if (pci->ops && pci->ops->write_dbi) {
		pci->ops->write_dbi(pci, pci->atu_base, reg, 4, val);
		return;
	}

	ret = dw_pcie_write(pci->atu_base + reg, 4, val);
	if (ret)
		dev_err(pci->dev, "Write ATU address failed\n");
}

static u32 dw_pcie_readl_ob_unroll(struct dw_pcie *pci, u32 index, u32 reg)
{
	u32 offset = PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(index);

	return dw_pcie_readl_atu(pci, offset + reg);
}

static void dw_pcie_writel_ob_unroll(struct dw_pcie *pci, u32 index, u32 reg,
				     u32 val)
{
	u32 offset = PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(index);

	dw_pcie_writel_atu(pci, offset + reg, val);
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

static void dw_pcie_prog_outbound_atu_unroll(struct dw_pcie *pci, u8 func_no,
					     int index, int type,
					     u64 cpu_addr, u64 pci_addr,
					     u64 size)
{
	u32 retries, val;
	u64 limit_addr = cpu_addr + size - 1;

	dw_pcie_writel_ob_unroll(pci, index, PCIE_ATU_UNR_LOWER_BASE,
				 lower_32_bits(cpu_addr));
	dw_pcie_writel_ob_unroll(pci, index, PCIE_ATU_UNR_UPPER_BASE,
				 upper_32_bits(cpu_addr));
	dw_pcie_writel_ob_unroll(pci, index, PCIE_ATU_UNR_LOWER_LIMIT,
				 lower_32_bits(limit_addr));
	dw_pcie_writel_ob_unroll(pci, index, PCIE_ATU_UNR_UPPER_LIMIT,
				 upper_32_bits(limit_addr));
	dw_pcie_writel_ob_unroll(pci, index, PCIE_ATU_UNR_LOWER_TARGET,
				 lower_32_bits(pci_addr));
	dw_pcie_writel_ob_unroll(pci, index, PCIE_ATU_UNR_UPPER_TARGET,
				 upper_32_bits(pci_addr));
	val = type | PCIE_ATU_FUNC_NUM(func_no);
	val = upper_32_bits(size - 1) ?
		val | PCIE_ATU_INCREASE_REGION_SIZE : val;
	if (pci->version == 0x490A)
		val = dw_pcie_enable_ecrc(val);
	dw_pcie_writel_ob_unroll(pci, index, PCIE_ATU_UNR_REGION_CTRL1, val);
	dw_pcie_writel_ob_unroll(pci, index, PCIE_ATU_UNR_REGION_CTRL2,
				 PCIE_ATU_ENABLE);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie_readl_ob_unroll(pci, index,
					      PCIE_ATU_UNR_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return;

		mdelay(LINK_WAIT_IATU);
	}
	dev_err(pci->dev, "Outbound iATU is not being enabled\n");
}

static void __dw_pcie_prog_outbound_atu(struct dw_pcie *pci, u8 func_no,
					int index, int type, u64 cpu_addr,
					u64 pci_addr, u64 size)
{
	u32 retries, val;

	if (pci->ops && pci->ops->cpu_addr_fixup)
		cpu_addr = pci->ops->cpu_addr_fixup(pci, cpu_addr);

	if (pci->iatu_unroll_enabled) {
		dw_pcie_prog_outbound_atu_unroll(pci, func_no, index, type,
						 cpu_addr, pci_addr, size);
		return;
	}

	dw_pcie_writel_dbi(pci, PCIE_ATU_VIEWPORT,
			   PCIE_ATU_REGION_OUTBOUND | index);
	dw_pcie_writel_dbi(pci, PCIE_ATU_LOWER_BASE,
			   lower_32_bits(cpu_addr));
	dw_pcie_writel_dbi(pci, PCIE_ATU_UPPER_BASE,
			   upper_32_bits(cpu_addr));
	dw_pcie_writel_dbi(pci, PCIE_ATU_LIMIT,
			   lower_32_bits(cpu_addr + size - 1));
	if (pci->version >= 0x460A)
		dw_pcie_writel_dbi(pci, PCIE_ATU_UPPER_LIMIT,
				   upper_32_bits(cpu_addr + size - 1));
	dw_pcie_writel_dbi(pci, PCIE_ATU_LOWER_TARGET,
			   lower_32_bits(pci_addr));
	dw_pcie_writel_dbi(pci, PCIE_ATU_UPPER_TARGET,
			   upper_32_bits(pci_addr));
	val = type | PCIE_ATU_FUNC_NUM(func_no);
	val = ((upper_32_bits(size - 1)) && (pci->version >= 0x460A)) ?
		val | PCIE_ATU_INCREASE_REGION_SIZE : val;
	if (pci->version == 0x490A)
		val = dw_pcie_enable_ecrc(val);
	dw_pcie_writel_dbi(pci, PCIE_ATU_CR1, val);
	dw_pcie_writel_dbi(pci, PCIE_ATU_CR2, PCIE_ATU_ENABLE);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie_readl_dbi(pci, PCIE_ATU_CR2);
		if (val & PCIE_ATU_ENABLE)
			return;

		mdelay(LINK_WAIT_IATU);
	}
	dev_err(pci->dev, "Outbound iATU is not being enabled\n");
}

void dw_pcie_prog_outbound_atu(struct dw_pcie *pci, int index, int type,
			       u64 cpu_addr, u64 pci_addr, u64 size)
{
	__dw_pcie_prog_outbound_atu(pci, 0, index, type,
				    cpu_addr, pci_addr, size);
}

void dw_pcie_prog_ep_outbound_atu(struct dw_pcie *pci, u8 func_no, int index,
				  int type, u64 cpu_addr, u64 pci_addr,
				  u64 size)
{
	__dw_pcie_prog_outbound_atu(pci, func_no, index, type,
				    cpu_addr, pci_addr, size);
}

static u32 dw_pcie_readl_ib_unroll(struct dw_pcie *pci, u32 index, u32 reg)
{
	u32 offset = PCIE_GET_ATU_INB_UNR_REG_OFFSET(index);

	return dw_pcie_readl_atu(pci, offset + reg);
}

static void dw_pcie_writel_ib_unroll(struct dw_pcie *pci, u32 index, u32 reg,
				     u32 val)
{
	u32 offset = PCIE_GET_ATU_INB_UNR_REG_OFFSET(index);

	dw_pcie_writel_atu(pci, offset + reg, val);
}

static int dw_pcie_prog_inbound_atu_unroll(struct dw_pcie *pci, u8 func_no,
					   int index, int bar, u64 cpu_addr,
					   enum dw_pcie_as_type as_type)
{
	int type;
	u32 retries, val;

	dw_pcie_writel_ib_unroll(pci, index, PCIE_ATU_UNR_LOWER_TARGET,
				 lower_32_bits(cpu_addr));
	dw_pcie_writel_ib_unroll(pci, index, PCIE_ATU_UNR_UPPER_TARGET,
				 upper_32_bits(cpu_addr));

	switch (as_type) {
	case DW_PCIE_AS_MEM:
		type = PCIE_ATU_TYPE_MEM;
		break;
	case DW_PCIE_AS_IO:
		type = PCIE_ATU_TYPE_IO;
		break;
	default:
		return -EINVAL;
	}

	dw_pcie_writel_ib_unroll(pci, index, PCIE_ATU_UNR_REGION_CTRL1, type |
				 PCIE_ATU_FUNC_NUM(func_no));
	dw_pcie_writel_ib_unroll(pci, index, PCIE_ATU_UNR_REGION_CTRL2,
				 PCIE_ATU_FUNC_NUM_MATCH_EN |
				 PCIE_ATU_ENABLE |
				 PCIE_ATU_BAR_MODE_ENABLE | (bar << 8));

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie_readl_ib_unroll(pci, index,
					      PCIE_ATU_UNR_REGION_CTRL2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}
	dev_err(pci->dev, "Inbound iATU is not being enabled\n");

	return -EBUSY;
}

int dw_pcie_prog_inbound_atu(struct dw_pcie *pci, u8 func_no, int index,
			     int bar, u64 cpu_addr,
			     enum dw_pcie_as_type as_type)
{
	int type;
	u32 retries, val;

	if (pci->iatu_unroll_enabled)
		return dw_pcie_prog_inbound_atu_unroll(pci, func_no, index, bar,
						       cpu_addr, as_type);

	dw_pcie_writel_dbi(pci, PCIE_ATU_VIEWPORT, PCIE_ATU_REGION_INBOUND |
			   index);
	dw_pcie_writel_dbi(pci, PCIE_ATU_LOWER_TARGET, lower_32_bits(cpu_addr));
	dw_pcie_writel_dbi(pci, PCIE_ATU_UPPER_TARGET, upper_32_bits(cpu_addr));

	switch (as_type) {
	case DW_PCIE_AS_MEM:
		type = PCIE_ATU_TYPE_MEM;
		break;
	case DW_PCIE_AS_IO:
		type = PCIE_ATU_TYPE_IO;
		break;
	default:
		return -EINVAL;
	}

	dw_pcie_writel_dbi(pci, PCIE_ATU_CR1, type |
			   PCIE_ATU_FUNC_NUM(func_no));
	dw_pcie_writel_dbi(pci, PCIE_ATU_CR2, PCIE_ATU_ENABLE |
			   PCIE_ATU_FUNC_NUM_MATCH_EN |
			   PCIE_ATU_BAR_MODE_ENABLE | (bar << 8));

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie_readl_dbi(pci, PCIE_ATU_CR2);
		if (val & PCIE_ATU_ENABLE)
			return 0;

		mdelay(LINK_WAIT_IATU);
	}
	dev_err(pci->dev, "Inbound iATU is not being enabled\n");

	return -EBUSY;
}

void dw_pcie_disable_atu(struct dw_pcie *pci, int index,
			 enum dw_pcie_region_type type)
{
	int region;

	switch (type) {
	case DW_PCIE_REGION_INBOUND:
		region = PCIE_ATU_REGION_INBOUND;
		break;
	case DW_PCIE_REGION_OUTBOUND:
		region = PCIE_ATU_REGION_OUTBOUND;
		break;
	default:
		return;
	}

	dw_pcie_writel_dbi(pci, PCIE_ATU_VIEWPORT, region | index);
	dw_pcie_writel_dbi(pci, PCIE_ATU_CR2, ~(u32)PCIE_ATU_ENABLE);
}

int dw_pcie_wait_for_link(struct dw_pcie *pci)
{
	int retries;

	/* Check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (dw_pcie_link_up(pci)) {
			dev_info(pci->dev, "Link up\n");
			return 0;
		}
		usleep_range(LINK_WAIT_USLEEP_MIN, LINK_WAIT_USLEEP_MAX);
	}

	dev_info(pci->dev, "Phy link never came up\n");

	return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(dw_pcie_wait_for_link);

int dw_pcie_link_up(struct dw_pcie *pci)
{
	u32 val;

	if (pci->ops && pci->ops->link_up)
		return pci->ops->link_up(pci);

	val = readl(pci->dbi_base + PCIE_PORT_DEBUG1);
	return ((val & PCIE_PORT_DEBUG1_LINK_UP) &&
		(!(val & PCIE_PORT_DEBUG1_LINK_IN_TRAINING)));
}

void dw_pcie_upconfig_setup(struct dw_pcie *pci)
{
	u32 val;

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL);
	val |= PORT_MLTI_UPCFG_SUPPORT;
	dw_pcie_writel_dbi(pci, PCIE_PORT_MULTI_LANE_CTRL, val);
}
EXPORT_SYMBOL_GPL(dw_pcie_upconfig_setup);

static void dw_pcie_link_set_max_speed(struct dw_pcie *pci, u32 link_gen)
{
	u32 cap, ctrl2, link_speed;
	u8 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);

	cap = dw_pcie_readl_dbi(pci, offset + PCI_EXP_LNKCAP);
	ctrl2 = dw_pcie_readl_dbi(pci, offset + PCI_EXP_LNKCTL2);
	ctrl2 &= ~PCI_EXP_LNKCTL2_TLS;

	switch (pcie_link_speed[link_gen]) {
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

static u8 dw_pcie_iatu_unroll_enabled(struct dw_pcie *pci)
{
	u32 val;

	val = dw_pcie_readl_dbi(pci, PCIE_ATU_VIEWPORT);
	if (val == 0xffffffff)
		return 1;

	return 0;
}

static void dw_pcie_iatu_detect_regions_unroll(struct dw_pcie *pci)
{
	int max_region, i, ob = 0, ib = 0;
	u32 val;

	max_region = min((int)pci->atu_size / 512, 256);

	for (i = 0; i < max_region; i++) {
		dw_pcie_writel_ob_unroll(pci, i, PCIE_ATU_UNR_LOWER_TARGET,
					0x11110000);

		val = dw_pcie_readl_ob_unroll(pci, i, PCIE_ATU_UNR_LOWER_TARGET);
		if (val == 0x11110000)
			ob++;
		else
			break;
	}

	for (i = 0; i < max_region; i++) {
		dw_pcie_writel_ib_unroll(pci, i, PCIE_ATU_UNR_LOWER_TARGET,
					0x11110000);

		val = dw_pcie_readl_ib_unroll(pci, i, PCIE_ATU_UNR_LOWER_TARGET);
		if (val == 0x11110000)
			ib++;
		else
			break;
	}
	pci->num_ib_windows = ib;
	pci->num_ob_windows = ob;
}

static void dw_pcie_iatu_detect_regions(struct dw_pcie *pci)
{
	int max_region, i, ob = 0, ib = 0;
	u32 val;

	dw_pcie_writel_dbi(pci, PCIE_ATU_VIEWPORT, 0xFF);
	max_region = dw_pcie_readl_dbi(pci, PCIE_ATU_VIEWPORT) + 1;

	for (i = 0; i < max_region; i++) {
		dw_pcie_writel_dbi(pci, PCIE_ATU_VIEWPORT, PCIE_ATU_REGION_OUTBOUND | i);
		dw_pcie_writel_dbi(pci, PCIE_ATU_LOWER_TARGET, 0x11110000);
		val = dw_pcie_readl_dbi(pci, PCIE_ATU_LOWER_TARGET);
		if (val == 0x11110000)
			ob++;
		else
			break;
	}

	for (i = 0; i < max_region; i++) {
		dw_pcie_writel_dbi(pci, PCIE_ATU_VIEWPORT, PCIE_ATU_REGION_INBOUND | i);
		dw_pcie_writel_dbi(pci, PCIE_ATU_LOWER_TARGET, 0x11110000);
		val = dw_pcie_readl_dbi(pci, PCIE_ATU_LOWER_TARGET);
		if (val == 0x11110000)
			ib++;
		else
			break;
	}

	pci->num_ib_windows = ib;
	pci->num_ob_windows = ob;
}

void dw_pcie_iatu_detect(struct dw_pcie *pci)
{
	struct device *dev = pci->dev;
	struct platform_device *pdev = to_platform_device(dev);

	if (pci->version >= 0x480A || (!pci->version &&
				       dw_pcie_iatu_unroll_enabled(pci))) {
		pci->iatu_unroll_enabled = true;
		if (!pci->atu_base) {
			struct resource *res =
				platform_get_resource_byname(pdev, IORESOURCE_MEM, "atu");
			if (res)
				pci->atu_size = resource_size(res);
			pci->atu_base = devm_ioremap_resource(dev, res);
			if (IS_ERR(pci->atu_base))
				pci->atu_base = pci->dbi_base + DEFAULT_DBI_ATU_OFFSET;
		}

		if (!pci->atu_size)
			/* Pick a minimal default, enough for 8 in and 8 out windows */
			pci->atu_size = SZ_4K;

		dw_pcie_iatu_detect_regions_unroll(pci);
	} else
		dw_pcie_iatu_detect_regions(pci);

	dev_info(pci->dev, "iATU unroll: %s\n", pci->iatu_unroll_enabled ?
		"enabled" : "disabled");

	dev_info(pci->dev, "Detected iATU regions: %u outbound, %u inbound",
		 pci->num_ob_windows, pci->num_ib_windows);
}

void dw_pcie_setup(struct dw_pcie *pci)
{
	u32 val;
	struct device *dev = pci->dev;
	struct device_node *np = dev->of_node;

	if (pci->link_gen > 0)
		dw_pcie_link_set_max_speed(pci, pci->link_gen);

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
		val |= pci->n_fts[pci->link_gen - 1];
		dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);
	}

	val = dw_pcie_readl_dbi(pci, PCIE_PORT_LINK_CONTROL);
	val &= ~PORT_LINK_FAST_LINK_MODE;
	val |= PORT_LINK_DLL_LINK_EN;
	dw_pcie_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, val);

	of_property_read_u32(np, "num-lanes", &pci->num_lanes);
	if (!pci->num_lanes) {
		dev_dbg(pci->dev, "Using h/w default number of lanes\n");
		return;
	}

	/* Set the number of lanes */
	val &= ~PORT_LINK_FAST_LINK_MODE;
	val &= ~PORT_LINK_MODE_MASK;
	switch (pci->num_lanes) {
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
		dev_err(pci->dev, "num-lanes %u: invalid value\n", pci->num_lanes);
		return;
	}
	dw_pcie_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, val);

	/* Set link width speed control register */
	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val &= ~PORT_LOGIC_LINK_WIDTH_MASK;
	switch (pci->num_lanes) {
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
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	if (of_property_read_bool(np, "snps,enable-cdm-check")) {
		val = dw_pcie_readl_dbi(pci, PCIE_PL_CHK_REG_CONTROL_STATUS);
		val |= PCIE_PL_CHK_REG_CHK_REG_CONTINUOUS |
		       PCIE_PL_CHK_REG_CHK_REG_START;
		dw_pcie_writel_dbi(pci, PCIE_PL_CHK_REG_CONTROL_STATUS, val);
	}
}
