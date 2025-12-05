// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence PCIe controller driver.
 *
 * Copyright (c) 2024, Cadence Design Systems
 * Author: Manikandan K Pillai <mpillai@cadence.com>
 */
#include <linux/kernel.h>
#include <linux/of.h>

#include "pcie-cadence.h"

bool cdns_pcie_hpa_link_up(struct cdns_pcie *pcie)
{
	u32 pl_reg_val;

	pl_reg_val = cdns_pcie_hpa_readl(pcie, REG_BANK_IP_REG, CDNS_PCIE_HPA_PHY_DBG_STS_REG0);
	if (pl_reg_val & GENMASK(0, 0))
		return true;
	return false;
}
EXPORT_SYMBOL_GPL(cdns_pcie_hpa_link_up);

void cdns_pcie_hpa_detect_quiet_min_delay_set(struct cdns_pcie *pcie)
{
	u32 delay = 0x3;
	u32 ltssm_control_cap;

	/* Set the LTSSM Detect Quiet state min. delay to 2ms */
	ltssm_control_cap = cdns_pcie_hpa_readl(pcie, REG_BANK_IP_REG,
						CDNS_PCIE_HPA_PHY_LAYER_CFG0);
	ltssm_control_cap = ((ltssm_control_cap &
			    ~CDNS_PCIE_HPA_DETECT_QUIET_MIN_DELAY_MASK) |
			    CDNS_PCIE_HPA_DETECT_QUIET_MIN_DELAY(delay));

	cdns_pcie_hpa_writel(pcie, REG_BANK_IP_REG,
			     CDNS_PCIE_HPA_PHY_LAYER_CFG0, ltssm_control_cap);
}
EXPORT_SYMBOL_GPL(cdns_pcie_hpa_detect_quiet_min_delay_set);

void cdns_pcie_hpa_set_outbound_region(struct cdns_pcie *pcie, u8 busnr, u8 fn,
				       u32 r, bool is_io,
				       u64 cpu_addr, u64 pci_addr, size_t size)
{
	/*
	 * roundup_pow_of_two() returns an unsigned long, which is not suited
	 * for 64bit values
	 */
	u64 sz = 1ULL << fls64(size - 1);
	int nbits = ilog2(sz);
	u32 addr0, addr1, desc0, desc1, ctrl0;

	if (nbits < 8)
		nbits = 8;

	/* Set the PCI address */
	addr0 = CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0_NBITS(nbits) |
		(lower_32_bits(pci_addr) & GENMASK(31, 8));
	addr1 = upper_32_bits(pci_addr);

	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0(r), addr0);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR1(r), addr1);

	/* Set the PCIe header descriptor */
	if (is_io)
		desc0 = CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_IO;
	else
		desc0 = CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_MEM;
	desc1 = 0;
	ctrl0 = 0;

	/*
	 * Whether Bit [26] is set or not inside DESC0 register of the outbound
	 * PCIe descriptor, the PCI function number must be set into
	 * Bits [31:24] of DESC1 anyway.
	 *
	 * In Root Complex mode, the function number is always 0 but in Endpoint
	 * mode, the PCIe controller may support more than one function. This
	 * function number needs to be set properly into the outbound PCIe
	 * descriptor.
	 *
	 * Besides, setting Bit [26] is mandatory when in Root Complex mode:
	 * then the driver must provide the bus, resp. device, number in
	 * Bits [31:24] of DESC1, resp. Bits[23:16] of DESC0. Like the function
	 * number, the device number is always 0 in Root Complex mode.
	 *
	 * However when in Endpoint mode, we can clear Bit [26] of DESC0, hence
	 * the PCIe controller will use the captured values for the bus and
	 * device numbers.
	 */
	if (pcie->is_rc) {
		/* The device and function numbers are always 0 */
		desc1 = CDNS_PCIE_HPA_AT_OB_REGION_DESC1_BUS(busnr) |
			CDNS_PCIE_HPA_AT_OB_REGION_DESC1_DEVFN(0);
		ctrl0 = CDNS_PCIE_HPA_AT_OB_REGION_CTRL0_SUPPLY_BUS |
			CDNS_PCIE_HPA_AT_OB_REGION_CTRL0_SUPPLY_DEV_FN;
	} else {
		/*
		 * Use captured values for bus and device numbers but still
		 * need to set the function number
		 */
		desc1 |= CDNS_PCIE_HPA_AT_OB_REGION_DESC1_DEVFN(fn);
	}

	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_DESC0(r), desc0);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_DESC1(r), desc1);

	addr0 = CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR0_NBITS(nbits) |
		(lower_32_bits(cpu_addr) & GENMASK(31, 8));
	addr1 = upper_32_bits(cpu_addr);

	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR0(r), addr0);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR1(r), addr1);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_CTRL0(r), ctrl0);
}
EXPORT_SYMBOL_GPL(cdns_pcie_hpa_set_outbound_region);

void cdns_pcie_hpa_set_outbound_region_for_normal_msg(struct cdns_pcie *pcie,
						      u8 busnr, u8 fn,
						      u32 r, u64 cpu_addr)
{
	u32 addr0, addr1, desc0, desc1, ctrl0;

	desc0 = CDNS_PCIE_HPA_AT_OB_REGION_DESC0_TYPE_NORMAL_MSG;
	desc1 = 0;
	ctrl0 = 0;

	/* See cdns_pcie_set_outbound_region() comments above */
	if (pcie->is_rc) {
		desc1 = CDNS_PCIE_HPA_AT_OB_REGION_DESC1_BUS(busnr) |
			CDNS_PCIE_HPA_AT_OB_REGION_DESC1_DEVFN(0);
		ctrl0 = CDNS_PCIE_HPA_AT_OB_REGION_CTRL0_SUPPLY_BUS |
			CDNS_PCIE_HPA_AT_OB_REGION_CTRL0_SUPPLY_DEV_FN;
	} else {
		desc1 |= CDNS_PCIE_HPA_AT_OB_REGION_DESC1_DEVFN(fn);
	}

	addr0 = CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR0_NBITS(17) |
		(lower_32_bits(cpu_addr) & GENMASK(31, 8));
	addr1 = upper_32_bits(cpu_addr);

	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR0(r), 0);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_PCI_ADDR1(r), 0);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_DESC0(r), desc0);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_DESC1(r), desc1);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR0(r), addr0);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_CPU_ADDR1(r), addr1);
	cdns_pcie_hpa_writel(pcie, REG_BANK_AXI_SLAVE,
			     CDNS_PCIE_HPA_AT_OB_REGION_CTRL0(r), ctrl0);
}
EXPORT_SYMBOL_GPL(cdns_pcie_hpa_set_outbound_region_for_normal_msg);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cadence PCIe controller driver");
