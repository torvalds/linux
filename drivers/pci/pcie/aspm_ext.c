// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip PCIe Apis For WIFI
 *
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/aspm_ext.h>
#include <linux/errno.h>


static u32 rockchip_pcie_pcie_access_cap(struct pci_dev *pdev, int cap, uint offset,
					 bool is_ext, bool is_write, u32 writeval)
{
	int cap_ptr = 0;
	u32 ret = -1;
	u32 readval;

	if (!(pdev)) {
		pci_err(pdev, "%s: pdev is NULL\n", __func__);
		return ret;
	}

	/* Find Capability offset */
	if (is_ext) {
		/* removing max EXT_CAP_ID check as
		 * linux kernel definition's max value is not updated yet as per spec
		 */
		cap_ptr = pci_find_ext_capability(pdev, cap);

	} else {
		/* removing max PCI_CAP_ID_MAX check as
		 * previous kernel versions dont have this definition
		 */
		cap_ptr = pci_find_capability(pdev, cap);
	}

	/* Return if capability with given ID not found */
	if (cap_ptr == 0) {
		pci_err(pdev, "%s: PCI Cap(0x%02x) not supported.\n",
		       __func__, cap);
		return -EINVAL;
	}

	if (is_write) {
		pci_write_config_dword(pdev, (cap_ptr + offset), writeval);
		ret = 0;

	} else {
		pci_read_config_dword(pdev, (cap_ptr + offset), &readval);
		ret = readval;
	}

	return ret;
}

static bool rockchip_pcie_bus_aspm_enable_dev(char *device, struct pci_dev *dev, bool enable)
{
	u32 linkctrl_before;
	u32 linkctrl_after = 0;
	u8 linkctrl_asm;

	linkctrl_before = rockchip_pcie_pcie_access_cap(dev, PCI_CAP_ID_EXP, PCI_EXP_LNKCTL,
							false, false, 0);
	linkctrl_asm = (linkctrl_before & PCI_EXP_LNKCTL_ASPMC);

	if (enable) {
		if (linkctrl_asm == PCI_EXP_LNKCTL_ASPM_L1) {
			pci_err(dev, "%s: %s already enabled  linkctrl: 0x%x\n",
			       __func__, device, linkctrl_before);
			return false;
		}
		/* Enable only L1 ASPM (bit 1) */
		rockchip_pcie_pcie_access_cap(dev, PCI_CAP_ID_EXP, PCI_EXP_LNKCTL, false,
					      true, (linkctrl_before | PCI_EXP_LNKCTL_ASPM_L1));
	} else {
		if (linkctrl_asm == 0) {
			pci_err(dev, "%s: %s already disabled linkctrl: 0x%x\n",
			       __func__, device, linkctrl_before);
			return false;
		}
		/* Disable complete ASPM (bit 1 and bit 0) */
		rockchip_pcie_pcie_access_cap(dev, PCI_CAP_ID_EXP, PCI_EXP_LNKCTL, false,
					      true, (linkctrl_before & (~PCI_EXP_LNKCTL_ASPMC)));
	}

	linkctrl_after = rockchip_pcie_pcie_access_cap(dev, PCI_CAP_ID_EXP, PCI_EXP_LNKCTL,
						       false, false, 0);
	pci_err(dev, "%s: %s %s, linkctrl_before: 0x%x linkctrl_after: 0x%x\n",
	       __func__, device, (enable ? "ENABLE " : "DISABLE"),
		linkctrl_before, linkctrl_after);

	return true;
}

bool rockchip_pcie_bus_aspm_enable_rc_ep(struct pci_dev *child, struct pci_dev *parent, bool enable)
{
	bool ret;

	if (enable) {
		/* Enable only L1 ASPM first RC then EP */
		ret = rockchip_pcie_bus_aspm_enable_dev("RC", parent, enable);
		ret = rockchip_pcie_bus_aspm_enable_dev("EP", child, enable);
	} else {
		/* Disable complete ASPM first EP then RC */
		ret = rockchip_pcie_bus_aspm_enable_dev("EP", child, enable);
		ret = rockchip_pcie_bus_aspm_enable_dev("RC", parent, enable);
	}

	return ret;
}

static void pci_clear_and_set_dword(struct pci_dev *pdev, int pos,
				    u32 clear, u32 set)
{
	u32 val;

	pci_read_config_dword(pdev, pos, &val);
	val &= ~clear;
	val |= set;
	pci_write_config_dword(pdev, pos, val);
}

/* Convert L1SS T_pwr encoding to usec */
static u32 calc_l1ss_pwron(struct pci_dev *pdev, u32 scale, u32 val)
{
	switch (scale) {
	case 0:
		return val * 2;
	case 1:
		return val * 10;
	case 2:
		return val * 100;
	}

	return 0;
}

static void encode_l12_threshold(u32 threshold_us, u32 *scale, u32 *value)
{
	u32 threshold_ns = threshold_us * 1000;

	/* See PCIe r3.1, sec 7.33.3 and sec 6.18 */
	if (threshold_ns < 32) {
		*scale = 0;
		*value = threshold_ns;
	} else if (threshold_ns < 1024) {
		*scale = 1;
		*value = threshold_ns >> 5;
	} else if (threshold_ns < 32768) {
		*scale = 2;
		*value = threshold_ns >> 10;
	} else if (threshold_ns < 1048576) {
		*scale = 3;
		*value = threshold_ns >> 15;
	} else if (threshold_ns < 33554432) {
		*scale = 4;
		*value = threshold_ns >> 20;
	} else {
		*scale = 5;
		*value = threshold_ns >> 25;
	}
}

/* Calculate L1.2 PM substate timing parameters */
static void aspm_calc_l1ss_info(struct pci_dev *child, struct pci_dev *parent)
{
	u32 val1, val2, scale1, scale2;
	u32 t_common_mode, t_power_on, l1_2_threshold, scale, value;
	u32 ctl1 = 0, ctl2 = 0;
	u32 pctl1, pctl2, cctl1, cctl2;
	u32 pl1_2_enables, cl1_2_enables;
	u32 parent_l1ss_cap, child_l1ss_cap;

	/* Setup L1 substate */
	pci_read_config_dword(parent, parent->l1ss + PCI_L1SS_CAP,
			      &parent_l1ss_cap);
	pci_read_config_dword(child, child->l1ss + PCI_L1SS_CAP,
			      &child_l1ss_cap);

	/* Choose the greater of the two Port Common_Mode_Restore_Times */
	val1 = (parent_l1ss_cap & PCI_L1SS_CAP_CM_RESTORE_TIME) >> 8;
	val2 = (child_l1ss_cap & PCI_L1SS_CAP_CM_RESTORE_TIME) >> 8;
	t_common_mode = max(val1, val2);

	/* Choose the greater of the two Port T_POWER_ON times */
	val1   = (parent_l1ss_cap & PCI_L1SS_CAP_P_PWR_ON_VALUE) >> 19;
	scale1 = (parent_l1ss_cap & PCI_L1SS_CAP_P_PWR_ON_SCALE) >> 16;
	val2   = (child_l1ss_cap & PCI_L1SS_CAP_P_PWR_ON_VALUE) >> 19;
	scale2 = (child_l1ss_cap & PCI_L1SS_CAP_P_PWR_ON_SCALE) >> 16;

	if (calc_l1ss_pwron(parent, scale1, val1) >
	    calc_l1ss_pwron(child, scale2, val2)) {
		ctl2 |= scale1 | (val1 << 3);
		t_power_on = calc_l1ss_pwron(parent, scale1, val1);
	} else {
		ctl2 |= scale2 | (val2 << 3);
		t_power_on = calc_l1ss_pwron(child, scale2, val2);
	}

	/* Set LTR_L1.2_THRESHOLD to the time required to transition the
	 * Link from L0 to L1.2 and back to L0 so we enter L1.2 only if
	 * downstream devices report (via LTR) that they can tolerate at
	 * least that much latency.
	 *
	 * Based on PCIe r3.1, sec 5.5.3.3.1, Figures 5-16 and 5-17, and
	 * Table 5-11.  T(POWER_OFF) is at most 2us and T(L1.2) is at
	 * least 4us.
	 */
	l1_2_threshold = 2 + 4 + t_common_mode + t_power_on;
	encode_l12_threshold(l1_2_threshold, &scale, &value);
	ctl1 |= t_common_mode << 8 | scale << 29 | value << 16;

	pci_read_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1, &pctl1);
	pci_read_config_dword(parent, parent->l1ss + PCI_L1SS_CTL2, &pctl2);
	pci_read_config_dword(child, child->l1ss + PCI_L1SS_CTL1, &cctl1);
	pci_read_config_dword(child, child->l1ss + PCI_L1SS_CTL2, &cctl2);

	if (ctl1 == pctl1 && ctl1 == cctl1 &&
	    ctl2 == pctl2 && ctl2 == cctl2)
		return;

	/* Disable L1.2 while updating.  See PCIe r5.0, sec 5.5.4, 7.8.3.3 */
	pl1_2_enables = pctl1 & PCI_L1SS_CTL1_L1_2_MASK;
	cl1_2_enables = cctl1 & PCI_L1SS_CTL1_L1_2_MASK;

	if (pl1_2_enables || cl1_2_enables) {
		pci_clear_and_set_dword(child, child->l1ss + PCI_L1SS_CTL1,
					PCI_L1SS_CTL1_L1_2_MASK, 0);
		pci_clear_and_set_dword(parent, parent->l1ss + PCI_L1SS_CTL1,
					PCI_L1SS_CTL1_L1_2_MASK, 0);
	}

	/* Program T_POWER_ON times in both ports */
	pci_write_config_dword(parent, parent->l1ss + PCI_L1SS_CTL2, ctl2);
	pci_write_config_dword(child, child->l1ss + PCI_L1SS_CTL2, ctl2);

	/* Program Common_Mode_Restore_Time in upstream device */
	pci_clear_and_set_dword(parent, parent->l1ss + PCI_L1SS_CTL1,
				PCI_L1SS_CTL1_CM_RESTORE_TIME, ctl1);

	/* Program LTR_L1.2_THRESHOLD time in both ports */
	pci_clear_and_set_dword(parent,	parent->l1ss + PCI_L1SS_CTL1,
				PCI_L1SS_CTL1_LTR_L12_TH_VALUE |
				PCI_L1SS_CTL1_LTR_L12_TH_SCALE, ctl1);
	pci_clear_and_set_dword(child, child->l1ss + PCI_L1SS_CTL1,
				PCI_L1SS_CTL1_LTR_L12_TH_VALUE |
				PCI_L1SS_CTL1_LTR_L12_TH_SCALE, ctl1);

	if (pl1_2_enables || cl1_2_enables) {
		pci_clear_and_set_dword(parent, parent->l1ss + PCI_L1SS_CTL1, 0,
					pl1_2_enables);
		pci_clear_and_set_dword(child, child->l1ss + PCI_L1SS_CTL1, 0,
					cl1_2_enables);
	}
}

static void rockchip_pcie_bus_l1ss_enable_dev(char *device, struct pci_dev *dev, bool enable)
{
	u32 l1ssctrl_before;
	u32 l1ssctrl_after = 0;
	u8 l1ss_ep;

	/* Extendend Capacility Reg */
	l1ssctrl_before = rockchip_pcie_pcie_access_cap(dev, PCI_EXT_CAP_ID_L1SS,
							PCI_L1SS_CTL1, true, false, 0);
	l1ss_ep = (l1ssctrl_before & PCI_L1SS_CTL1_L1SS_MASK);

	if (enable) {
		if (l1ss_ep == PCI_L1SS_CTL1_L1SS_MASK) {
			pci_err(dev, "%s: %s already enabled,  l1ssctrl: 0x%x\n",
			       __func__, device, l1ssctrl_before);
			return;
		}
		rockchip_pcie_pcie_access_cap(dev, PCI_EXT_CAP_ID_L1SS, PCI_L1SS_CTL1,
					      true, true, (l1ssctrl_before | PCI_L1SS_CTL1_L1SS_MASK));
	} else {
		if (l1ss_ep == 0) {
			pci_err(dev, "%s: %s already disabled, l1ssctrl: 0x%x\n",
			       __func__, device, l1ssctrl_before);
			return;
		}
		rockchip_pcie_pcie_access_cap(dev, PCI_EXT_CAP_ID_L1SS, PCI_L1SS_CTL1,
					      true, true, (l1ssctrl_before & (~PCI_L1SS_CTL1_L1SS_MASK)));
	}
	l1ssctrl_after = rockchip_pcie_pcie_access_cap(dev, PCI_EXT_CAP_ID_L1SS,
						       PCI_L1SS_CTL1, true, false, 0);
	pci_err(dev, "%s: %s %s, l1ssctrl_before: 0x%x l1ssctrl_after: 0x%x\n",
	       __func__, device, (enable ? "ENABLE " : "DISABLE"),
		l1ssctrl_before, l1ssctrl_after);
}

bool pcie_aspm_ext_is_rc_ep_l1ss_capable(struct pci_dev *child, struct pci_dev *parent)
{
	u32 parent_l1ss_cap, child_l1ss_cap;

	/* Setup L1 substate */
	pci_read_config_dword(parent, parent->l1ss + PCI_L1SS_CAP,
			      &parent_l1ss_cap);
	pci_read_config_dword(child, child->l1ss + PCI_L1SS_CAP,
			      &child_l1ss_cap);

	if (!(parent_l1ss_cap & PCI_L1SS_CAP_L1_PM_SS))
		parent_l1ss_cap = 0;
	if (!(child_l1ss_cap & PCI_L1SS_CAP_L1_PM_SS))
		child_l1ss_cap = 0;

	if (parent_l1ss_cap && child_l1ss_cap)
		return true;
	else
		return false;
}
EXPORT_SYMBOL(pcie_aspm_ext_is_rc_ep_l1ss_capable);

void pcie_aspm_ext_l1ss_enable(struct pci_dev *child, struct pci_dev *parent, bool enable)
{
	bool ret;

	/* Disable ASPM of RC and EP */
	ret = rockchip_pcie_bus_aspm_enable_rc_ep(child, parent, false);

	if (enable) {
		/* Enable RC then EP */
		aspm_calc_l1ss_info(child, parent);
		rockchip_pcie_bus_l1ss_enable_dev("RC", parent, enable);
		rockchip_pcie_bus_l1ss_enable_dev("EP", child, enable);
	} else {
		/* Disable EP then RC */
		rockchip_pcie_bus_l1ss_enable_dev("EP", child, enable);
		rockchip_pcie_bus_l1ss_enable_dev("RC", parent, enable);
	}

	/* Enable ASPM of RC and EP only if this API disabled */
	if (ret)
		rockchip_pcie_bus_aspm_enable_rc_ep(child, parent, true);
}
EXPORT_SYMBOL(pcie_aspm_ext_l1ss_enable);
