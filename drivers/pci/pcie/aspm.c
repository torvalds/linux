// SPDX-License-Identifier: GPL-2.0
/*
 * Enable PCIe link L0s/L1 state and Clock Power Management
 *
 * Copyright (C) 2007 Intel
 * Copyright (C) Zhang Yanmin (yanmin.zhang@intel.com)
 * Copyright (C) Shaohua Li (shaohua.li@intel.com)
 */

#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/time.h>

#include "../pci.h"

void pci_save_ltr_state(struct pci_dev *dev)
{
	int ltr;
	struct pci_cap_saved_state *save_state;
	u32 *cap;

	if (!pci_is_pcie(dev))
		return;

	ltr = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_LTR);
	if (!ltr)
		return;

	save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_LTR);
	if (!save_state) {
		pci_err(dev, "no suspend buffer for LTR; ASPM issues possible after resume\n");
		return;
	}

	/* Some broken devices only support dword access to LTR */
	cap = &save_state->cap.data[0];
	pci_read_config_dword(dev, ltr + PCI_LTR_MAX_SNOOP_LAT, cap);
}

void pci_restore_ltr_state(struct pci_dev *dev)
{
	struct pci_cap_saved_state *save_state;
	int ltr;
	u32 *cap;

	save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_LTR);
	ltr = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_LTR);
	if (!save_state || !ltr)
		return;

	/* Some broken devices only support dword access to LTR */
	cap = &save_state->cap.data[0];
	pci_write_config_dword(dev, ltr + PCI_LTR_MAX_SNOOP_LAT, *cap);
}

void pci_configure_aspm_l1ss(struct pci_dev *pdev)
{
	int rc;

	pdev->l1ss = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_L1SS);

	rc = pci_add_ext_cap_save_buffer(pdev, PCI_EXT_CAP_ID_L1SS,
					 2 * sizeof(u32));
	if (rc)
		pci_err(pdev, "unable to allocate ASPM L1SS save buffer (%pe)\n",
			ERR_PTR(rc));
}

void pci_save_aspm_l1ss_state(struct pci_dev *pdev)
{
	struct pci_cap_saved_state *save_state;
	u16 l1ss = pdev->l1ss;
	u32 *cap;

	/*
	 * Save L1 substate configuration. The ASPM L0s/L1 configuration
	 * in PCI_EXP_LNKCTL_ASPMC is saved by pci_save_pcie_state().
	 */
	if (!l1ss)
		return;

	save_state = pci_find_saved_ext_cap(pdev, PCI_EXT_CAP_ID_L1SS);
	if (!save_state)
		return;

	cap = &save_state->cap.data[0];
	pci_read_config_dword(pdev, l1ss + PCI_L1SS_CTL2, cap++);
	pci_read_config_dword(pdev, l1ss + PCI_L1SS_CTL1, cap++);
}

void pci_restore_aspm_l1ss_state(struct pci_dev *pdev)
{
	struct pci_cap_saved_state *pl_save_state, *cl_save_state;
	struct pci_dev *parent = pdev->bus->self;
	u32 *cap, pl_ctl1, pl_ctl2, pl_l1_2_enable;
	u32 cl_ctl1, cl_ctl2, cl_l1_2_enable;
	u16 clnkctl, plnkctl;

	/*
	 * In case BIOS enabled L1.2 when resuming, we need to disable it first
	 * on the downstream component before the upstream. So, don't attempt to
	 * restore either until we are at the downstream component.
	 */
	if (pcie_downstream_port(pdev) || !parent)
		return;

	if (!pdev->l1ss || !parent->l1ss)
		return;

	cl_save_state = pci_find_saved_ext_cap(pdev, PCI_EXT_CAP_ID_L1SS);
	pl_save_state = pci_find_saved_ext_cap(parent, PCI_EXT_CAP_ID_L1SS);
	if (!cl_save_state || !pl_save_state)
		return;

	cap = &cl_save_state->cap.data[0];
	cl_ctl2 = *cap++;
	cl_ctl1 = *cap;
	cap = &pl_save_state->cap.data[0];
	pl_ctl2 = *cap++;
	pl_ctl1 = *cap;

	/* Make sure L0s/L1 are disabled before updating L1SS config */
	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &clnkctl);
	pcie_capability_read_word(parent, PCI_EXP_LNKCTL, &plnkctl);
	if (FIELD_GET(PCI_EXP_LNKCTL_ASPMC, clnkctl) ||
	    FIELD_GET(PCI_EXP_LNKCTL_ASPMC, plnkctl)) {
		pcie_capability_write_word(pdev, PCI_EXP_LNKCTL,
					   clnkctl & ~PCI_EXP_LNKCTL_ASPMC);
		pcie_capability_write_word(parent, PCI_EXP_LNKCTL,
					   plnkctl & ~PCI_EXP_LNKCTL_ASPMC);
	}

	/*
	 * Disable L1.2 on this downstream endpoint device first, followed
	 * by the upstream
	 */
	pci_clear_and_set_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1,
				       PCI_L1SS_CTL1_L1_2_MASK, 0);
	pci_clear_and_set_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1,
				       PCI_L1SS_CTL1_L1_2_MASK, 0);

	/*
	 * In addition, Common_Mode_Restore_Time and LTR_L1.2_THRESHOLD
	 * in PCI_L1SS_CTL1 must be programmed *before* setting the L1.2
	 * enable bits, even though they're all in PCI_L1SS_CTL1.
	 */
	pl_l1_2_enable = pl_ctl1 & PCI_L1SS_CTL1_L1_2_MASK;
	pl_ctl1 &= ~PCI_L1SS_CTL1_L1_2_MASK;
	cl_l1_2_enable = cl_ctl1 & PCI_L1SS_CTL1_L1_2_MASK;
	cl_ctl1 &= ~PCI_L1SS_CTL1_L1_2_MASK;

	/* Write back without enables first (above we cleared them in ctl1) */
	pci_write_config_dword(parent, parent->l1ss + PCI_L1SS_CTL2, pl_ctl2);
	pci_write_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL2, cl_ctl2);
	pci_write_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1, pl_ctl1);
	pci_write_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, cl_ctl1);

	/* Then write back the enables */
	if (pl_l1_2_enable || cl_l1_2_enable) {
		pci_write_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1,
				       pl_ctl1 | pl_l1_2_enable);
		pci_write_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1,
				       cl_ctl1 | cl_l1_2_enable);
	}

	/* Restore L0s/L1 if they were enabled */
	if (FIELD_GET(PCI_EXP_LNKCTL_ASPMC, clnkctl) ||
	    FIELD_GET(PCI_EXP_LNKCTL_ASPMC, plnkctl)) {
		pcie_capability_write_word(parent, PCI_EXP_LNKCTL, plnkctl);
		pcie_capability_write_word(pdev, PCI_EXP_LNKCTL, clnkctl);
	}
}

#ifdef CONFIG_PCIEASPM

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "pcie_aspm."

/* Note: those are not register definitions */
#define ASPM_STATE_L0S_UP	(1)	/* Upstream direction L0s state */
#define ASPM_STATE_L0S_DW	(2)	/* Downstream direction L0s state */
#define ASPM_STATE_L1		(4)	/* L1 state */
#define ASPM_STATE_L1_1		(8)	/* ASPM L1.1 state */
#define ASPM_STATE_L1_2		(0x10)	/* ASPM L1.2 state */
#define ASPM_STATE_L1_1_PCIPM	(0x20)	/* PCI PM L1.1 state */
#define ASPM_STATE_L1_2_PCIPM	(0x40)	/* PCI PM L1.2 state */
#define ASPM_STATE_L1_SS_PCIPM	(ASPM_STATE_L1_1_PCIPM | ASPM_STATE_L1_2_PCIPM)
#define ASPM_STATE_L1_2_MASK	(ASPM_STATE_L1_2 | ASPM_STATE_L1_2_PCIPM)
#define ASPM_STATE_L1SS		(ASPM_STATE_L1_1 | ASPM_STATE_L1_1_PCIPM |\
				 ASPM_STATE_L1_2_MASK)
#define ASPM_STATE_L0S		(ASPM_STATE_L0S_UP | ASPM_STATE_L0S_DW)
#define ASPM_STATE_ALL		(ASPM_STATE_L0S | ASPM_STATE_L1 |	\
				 ASPM_STATE_L1SS)

struct pcie_link_state {
	struct pci_dev *pdev;		/* Upstream component of the Link */
	struct pci_dev *downstream;	/* Downstream component, function 0 */
	struct pcie_link_state *root;	/* pointer to the root port link */
	struct pcie_link_state *parent;	/* pointer to the parent Link state */
	struct list_head sibling;	/* node in link_list */

	/* ASPM state */
	u32 aspm_support:7;		/* Supported ASPM state */
	u32 aspm_enabled:7;		/* Enabled ASPM state */
	u32 aspm_capable:7;		/* Capable ASPM state with latency */
	u32 aspm_default:7;		/* Default ASPM state by BIOS */
	u32 aspm_disable:7;		/* Disabled ASPM state */

	/* Clock PM state */
	u32 clkpm_capable:1;		/* Clock PM capable? */
	u32 clkpm_enabled:1;		/* Current Clock PM state */
	u32 clkpm_default:1;		/* Default Clock PM state by BIOS */
	u32 clkpm_disable:1;		/* Clock PM disabled */
};

static int aspm_disabled, aspm_force;
static bool aspm_support_enabled = true;
static DEFINE_MUTEX(aspm_lock);
static LIST_HEAD(link_list);

#define POLICY_DEFAULT 0	/* BIOS default setting */
#define POLICY_PERFORMANCE 1	/* high performance */
#define POLICY_POWERSAVE 2	/* high power saving */
#define POLICY_POWER_SUPERSAVE 3 /* possibly even more power saving */

#ifdef CONFIG_PCIEASPM_PERFORMANCE
static int aspm_policy = POLICY_PERFORMANCE;
#elif defined CONFIG_PCIEASPM_POWERSAVE
static int aspm_policy = POLICY_POWERSAVE;
#elif defined CONFIG_PCIEASPM_POWER_SUPERSAVE
static int aspm_policy = POLICY_POWER_SUPERSAVE;
#else
static int aspm_policy;
#endif

static const char *policy_str[] = {
	[POLICY_DEFAULT] = "default",
	[POLICY_PERFORMANCE] = "performance",
	[POLICY_POWERSAVE] = "powersave",
	[POLICY_POWER_SUPERSAVE] = "powersupersave"
};

/*
 * The L1 PM substate capability is only implemented in function 0 in a
 * multi function device.
 */
static struct pci_dev *pci_function_0(struct pci_bus *linkbus)
{
	struct pci_dev *child;

	list_for_each_entry(child, &linkbus->devices, bus_list)
		if (PCI_FUNC(child->devfn) == 0)
			return child;
	return NULL;
}

static int policy_to_aspm_state(struct pcie_link_state *link)
{
	switch (aspm_policy) {
	case POLICY_PERFORMANCE:
		/* Disable ASPM and Clock PM */
		return 0;
	case POLICY_POWERSAVE:
		/* Enable ASPM L0s/L1 */
		return (ASPM_STATE_L0S | ASPM_STATE_L1);
	case POLICY_POWER_SUPERSAVE:
		/* Enable Everything */
		return ASPM_STATE_ALL;
	case POLICY_DEFAULT:
		return link->aspm_default;
	}
	return 0;
}

static int policy_to_clkpm_state(struct pcie_link_state *link)
{
	switch (aspm_policy) {
	case POLICY_PERFORMANCE:
		/* Disable ASPM and Clock PM */
		return 0;
	case POLICY_POWERSAVE:
	case POLICY_POWER_SUPERSAVE:
		/* Enable Clock PM */
		return 1;
	case POLICY_DEFAULT:
		return link->clkpm_default;
	}
	return 0;
}

static void pci_update_aspm_saved_state(struct pci_dev *dev)
{
	struct pci_cap_saved_state *save_state;
	u16 *cap, lnkctl, aspm_ctl;

	save_state = pci_find_saved_cap(dev, PCI_CAP_ID_EXP);
	if (!save_state)
		return;

	pcie_capability_read_word(dev, PCI_EXP_LNKCTL, &lnkctl);

	/*
	 * Update ASPM and CLKREQ bits of LNKCTL in save_state. We only
	 * write PCI_EXP_LNKCTL_CCC during enumeration, so it shouldn't
	 * change after being captured in save_state.
	 */
	aspm_ctl = lnkctl & (PCI_EXP_LNKCTL_ASPMC | PCI_EXP_LNKCTL_CLKREQ_EN);
	lnkctl &= ~(PCI_EXP_LNKCTL_ASPMC | PCI_EXP_LNKCTL_CLKREQ_EN);

	/* Depends on pci_save_pcie_state(): cap[1] is LNKCTL */
	cap = (u16 *)&save_state->cap.data[0];
	cap[1] = lnkctl | aspm_ctl;
}

static void pcie_set_clkpm_nocheck(struct pcie_link_state *link, int enable)
{
	struct pci_dev *child;
	struct pci_bus *linkbus = link->pdev->subordinate;
	u32 val = enable ? PCI_EXP_LNKCTL_CLKREQ_EN : 0;

	list_for_each_entry(child, &linkbus->devices, bus_list) {
		pcie_capability_clear_and_set_word(child, PCI_EXP_LNKCTL,
						   PCI_EXP_LNKCTL_CLKREQ_EN,
						   val);
		pci_update_aspm_saved_state(child);
	}
	link->clkpm_enabled = !!enable;
}

static void pcie_set_clkpm(struct pcie_link_state *link, int enable)
{
	/*
	 * Don't enable Clock PM if the link is not Clock PM capable
	 * or Clock PM is disabled
	 */
	if (!link->clkpm_capable || link->clkpm_disable)
		enable = 0;
	/* Need nothing if the specified equals to current state */
	if (link->clkpm_enabled == enable)
		return;
	pcie_set_clkpm_nocheck(link, enable);
}

static void pcie_clkpm_cap_init(struct pcie_link_state *link, int blacklist)
{
	int capable = 1, enabled = 1;
	u32 reg32;
	u16 reg16;
	struct pci_dev *child;
	struct pci_bus *linkbus = link->pdev->subordinate;

	/* All functions should have the same cap and state, take the worst */
	list_for_each_entry(child, &linkbus->devices, bus_list) {
		pcie_capability_read_dword(child, PCI_EXP_LNKCAP, &reg32);
		if (!(reg32 & PCI_EXP_LNKCAP_CLKPM)) {
			capable = 0;
			enabled = 0;
			break;
		}
		pcie_capability_read_word(child, PCI_EXP_LNKCTL, &reg16);
		if (!(reg16 & PCI_EXP_LNKCTL_CLKREQ_EN))
			enabled = 0;
	}
	link->clkpm_enabled = enabled;
	link->clkpm_default = enabled;
	link->clkpm_capable = capable;
	link->clkpm_disable = blacklist ? 1 : 0;
}

/*
 * pcie_aspm_configure_common_clock: check if the 2 ends of a link
 *   could use common clock. If they are, configure them to use the
 *   common clock. That will reduce the ASPM state exit latency.
 */
static void pcie_aspm_configure_common_clock(struct pcie_link_state *link)
{
	int same_clock = 1;
	u16 reg16, ccc, parent_old_ccc, child_old_ccc[8];
	struct pci_dev *child, *parent = link->pdev;
	struct pci_bus *linkbus = parent->subordinate;
	/*
	 * All functions of a slot should have the same Slot Clock
	 * Configuration, so just check one function
	 */
	child = list_entry(linkbus->devices.next, struct pci_dev, bus_list);
	BUG_ON(!pci_is_pcie(child));

	/* Check downstream component if bit Slot Clock Configuration is 1 */
	pcie_capability_read_word(child, PCI_EXP_LNKSTA, &reg16);
	if (!(reg16 & PCI_EXP_LNKSTA_SLC))
		same_clock = 0;

	/* Check upstream component if bit Slot Clock Configuration is 1 */
	pcie_capability_read_word(parent, PCI_EXP_LNKSTA, &reg16);
	if (!(reg16 & PCI_EXP_LNKSTA_SLC))
		same_clock = 0;

	/* Port might be already in common clock mode */
	pcie_capability_read_word(parent, PCI_EXP_LNKCTL, &reg16);
	parent_old_ccc = reg16 & PCI_EXP_LNKCTL_CCC;
	if (same_clock && (reg16 & PCI_EXP_LNKCTL_CCC)) {
		bool consistent = true;

		list_for_each_entry(child, &linkbus->devices, bus_list) {
			pcie_capability_read_word(child, PCI_EXP_LNKCTL,
						  &reg16);
			if (!(reg16 & PCI_EXP_LNKCTL_CCC)) {
				consistent = false;
				break;
			}
		}
		if (consistent)
			return;
		pci_info(parent, "ASPM: current common clock configuration is inconsistent, reconfiguring\n");
	}

	ccc = same_clock ? PCI_EXP_LNKCTL_CCC : 0;
	/* Configure downstream component, all functions */
	list_for_each_entry(child, &linkbus->devices, bus_list) {
		pcie_capability_read_word(child, PCI_EXP_LNKCTL, &reg16);
		child_old_ccc[PCI_FUNC(child->devfn)] = reg16 & PCI_EXP_LNKCTL_CCC;
		pcie_capability_clear_and_set_word(child, PCI_EXP_LNKCTL,
						   PCI_EXP_LNKCTL_CCC, ccc);
	}

	/* Configure upstream component */
	pcie_capability_clear_and_set_word(parent, PCI_EXP_LNKCTL,
					   PCI_EXP_LNKCTL_CCC, ccc);

	if (pcie_retrain_link(link->pdev, true)) {

		/* Training failed. Restore common clock configurations */
		pci_err(parent, "ASPM: Could not configure common clock\n");
		list_for_each_entry(child, &linkbus->devices, bus_list)
			pcie_capability_clear_and_set_word(child, PCI_EXP_LNKCTL,
							   PCI_EXP_LNKCTL_CCC,
							   child_old_ccc[PCI_FUNC(child->devfn)]);
		pcie_capability_clear_and_set_word(parent, PCI_EXP_LNKCTL,
						   PCI_EXP_LNKCTL_CCC, parent_old_ccc);
	}
}

/* Convert L0s latency encoding to ns */
static u32 calc_l0s_latency(u32 lnkcap)
{
	u32 encoding = FIELD_GET(PCI_EXP_LNKCAP_L0SEL, lnkcap);

	if (encoding == 0x7)
		return 5 * NSEC_PER_USEC;	/* > 4us */
	return (64 << encoding);
}

/* Convert L0s acceptable latency encoding to ns */
static u32 calc_l0s_acceptable(u32 encoding)
{
	if (encoding == 0x7)
		return U32_MAX;
	return (64 << encoding);
}

/* Convert L1 latency encoding to ns */
static u32 calc_l1_latency(u32 lnkcap)
{
	u32 encoding = FIELD_GET(PCI_EXP_LNKCAP_L1EL, lnkcap);

	if (encoding == 0x7)
		return 65 * NSEC_PER_USEC;	/* > 64us */
	return NSEC_PER_USEC << encoding;
}

/* Convert L1 acceptable latency encoding to ns */
static u32 calc_l1_acceptable(u32 encoding)
{
	if (encoding == 0x7)
		return U32_MAX;
	return NSEC_PER_USEC << encoding;
}

/* Convert L1SS T_pwr encoding to usec */
static u32 calc_l12_pwron(struct pci_dev *pdev, u32 scale, u32 val)
{
	switch (scale) {
	case 0:
		return val * 2;
	case 1:
		return val * 10;
	case 2:
		return val * 100;
	}
	pci_err(pdev, "%s: Invalid T_PwrOn scale: %u\n", __func__, scale);
	return 0;
}

/*
 * Encode an LTR_L1.2_THRESHOLD value for the L1 PM Substates Control 1
 * register.  Ports enter L1.2 when the most recent LTR value is greater
 * than or equal to LTR_L1.2_THRESHOLD, so we round up to make sure we
 * don't enter L1.2 too aggressively.
 *
 * See PCIe r6.0, sec 5.5.1, 6.18, 7.8.3.3.
 */
static void encode_l12_threshold(u32 threshold_us, u32 *scale, u32 *value)
{
	u64 threshold_ns = (u64)threshold_us * NSEC_PER_USEC;

	/*
	 * LTR_L1.2_THRESHOLD_Value ("value") is a 10-bit field with max
	 * value of 0x3ff.
	 */
	if (threshold_ns <= 1 * FIELD_MAX(PCI_L1SS_CTL1_LTR_L12_TH_VALUE)) {
		*scale = 0;		/* Value times 1ns */
		*value = threshold_ns;
	} else if (threshold_ns <= 32 * FIELD_MAX(PCI_L1SS_CTL1_LTR_L12_TH_VALUE)) {
		*scale = 1;		/* Value times 32ns */
		*value = roundup(threshold_ns, 32) / 32;
	} else if (threshold_ns <= 1024 * FIELD_MAX(PCI_L1SS_CTL1_LTR_L12_TH_VALUE)) {
		*scale = 2;		/* Value times 1024ns */
		*value = roundup(threshold_ns, 1024) / 1024;
	} else if (threshold_ns <= 32768 * FIELD_MAX(PCI_L1SS_CTL1_LTR_L12_TH_VALUE)) {
		*scale = 3;		/* Value times 32768ns */
		*value = roundup(threshold_ns, 32768) / 32768;
	} else if (threshold_ns <= 1048576 * FIELD_MAX(PCI_L1SS_CTL1_LTR_L12_TH_VALUE)) {
		*scale = 4;		/* Value times 1048576ns */
		*value = roundup(threshold_ns, 1048576) / 1048576;
	} else if (threshold_ns <= (u64)33554432 * FIELD_MAX(PCI_L1SS_CTL1_LTR_L12_TH_VALUE)) {
		*scale = 5;		/* Value times 33554432ns */
		*value = roundup(threshold_ns, 33554432) / 33554432;
	} else {
		*scale = 5;
		*value = FIELD_MAX(PCI_L1SS_CTL1_LTR_L12_TH_VALUE);
	}
}

static void pcie_aspm_check_latency(struct pci_dev *endpoint)
{
	u32 latency, encoding, lnkcap_up, lnkcap_dw;
	u32 l1_switch_latency = 0, latency_up_l0s;
	u32 latency_up_l1, latency_dw_l0s, latency_dw_l1;
	u32 acceptable_l0s, acceptable_l1;
	struct pcie_link_state *link;

	/* Device not in D0 doesn't need latency check */
	if ((endpoint->current_state != PCI_D0) &&
	    (endpoint->current_state != PCI_UNKNOWN))
		return;

	link = endpoint->bus->self->link_state;

	/* Calculate endpoint L0s acceptable latency */
	encoding = FIELD_GET(PCI_EXP_DEVCAP_L0S, endpoint->devcap);
	acceptable_l0s = calc_l0s_acceptable(encoding);

	/* Calculate endpoint L1 acceptable latency */
	encoding = FIELD_GET(PCI_EXP_DEVCAP_L1, endpoint->devcap);
	acceptable_l1 = calc_l1_acceptable(encoding);

	while (link) {
		struct pci_dev *dev = pci_function_0(link->pdev->subordinate);

		/* Read direction exit latencies */
		pcie_capability_read_dword(link->pdev, PCI_EXP_LNKCAP,
					   &lnkcap_up);
		pcie_capability_read_dword(dev, PCI_EXP_LNKCAP,
					   &lnkcap_dw);
		latency_up_l0s = calc_l0s_latency(lnkcap_up);
		latency_up_l1 = calc_l1_latency(lnkcap_up);
		latency_dw_l0s = calc_l0s_latency(lnkcap_dw);
		latency_dw_l1 = calc_l1_latency(lnkcap_dw);

		/* Check upstream direction L0s latency */
		if ((link->aspm_capable & ASPM_STATE_L0S_UP) &&
		    (latency_up_l0s > acceptable_l0s))
			link->aspm_capable &= ~ASPM_STATE_L0S_UP;

		/* Check downstream direction L0s latency */
		if ((link->aspm_capable & ASPM_STATE_L0S_DW) &&
		    (latency_dw_l0s > acceptable_l0s))
			link->aspm_capable &= ~ASPM_STATE_L0S_DW;
		/*
		 * Check L1 latency.
		 * Every switch on the path to root complex need 1
		 * more microsecond for L1. Spec doesn't mention L0s.
		 *
		 * The exit latencies for L1 substates are not advertised
		 * by a device.  Since the spec also doesn't mention a way
		 * to determine max latencies introduced by enabling L1
		 * substates on the components, it is not clear how to do
		 * a L1 substate exit latency check.  We assume that the
		 * L1 exit latencies advertised by a device include L1
		 * substate latencies (and hence do not do any check).
		 */
		latency = max_t(u32, latency_up_l1, latency_dw_l1);
		if ((link->aspm_capable & ASPM_STATE_L1) &&
		    (latency + l1_switch_latency > acceptable_l1))
			link->aspm_capable &= ~ASPM_STATE_L1;
		l1_switch_latency += NSEC_PER_USEC;

		link = link->parent;
	}
}

/* Calculate L1.2 PM substate timing parameters */
static void aspm_calc_l12_info(struct pcie_link_state *link,
				u32 parent_l1ss_cap, u32 child_l1ss_cap)
{
	struct pci_dev *child = link->downstream, *parent = link->pdev;
	u32 val1, val2, scale1, scale2;
	u32 t_common_mode, t_power_on, l1_2_threshold, scale, value;
	u32 ctl1 = 0, ctl2 = 0;
	u32 pctl1, pctl2, cctl1, cctl2;
	u32 pl1_2_enables, cl1_2_enables;

	/* Choose the greater of the two Port Common_Mode_Restore_Times */
	val1 = FIELD_GET(PCI_L1SS_CAP_CM_RESTORE_TIME, parent_l1ss_cap);
	val2 = FIELD_GET(PCI_L1SS_CAP_CM_RESTORE_TIME, child_l1ss_cap);
	t_common_mode = max(val1, val2);

	/* Choose the greater of the two Port T_POWER_ON times */
	val1   = FIELD_GET(PCI_L1SS_CAP_P_PWR_ON_VALUE, parent_l1ss_cap);
	scale1 = FIELD_GET(PCI_L1SS_CAP_P_PWR_ON_SCALE, parent_l1ss_cap);
	val2   = FIELD_GET(PCI_L1SS_CAP_P_PWR_ON_VALUE, child_l1ss_cap);
	scale2 = FIELD_GET(PCI_L1SS_CAP_P_PWR_ON_SCALE, child_l1ss_cap);

	if (calc_l12_pwron(parent, scale1, val1) >
	    calc_l12_pwron(child, scale2, val2)) {
		ctl2 |= FIELD_PREP(PCI_L1SS_CTL2_T_PWR_ON_SCALE, scale1) |
			FIELD_PREP(PCI_L1SS_CTL2_T_PWR_ON_VALUE, val1);
		t_power_on = calc_l12_pwron(parent, scale1, val1);
	} else {
		ctl2 |= FIELD_PREP(PCI_L1SS_CTL2_T_PWR_ON_SCALE, scale2) |
			FIELD_PREP(PCI_L1SS_CTL2_T_PWR_ON_VALUE, val2);
		t_power_on = calc_l12_pwron(child, scale2, val2);
	}

	/*
	 * Set LTR_L1.2_THRESHOLD to the time required to transition the
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
	ctl1 |= FIELD_PREP(PCI_L1SS_CTL1_CM_RESTORE_TIME, t_common_mode) |
		FIELD_PREP(PCI_L1SS_CTL1_LTR_L12_TH_VALUE, value) |
		FIELD_PREP(PCI_L1SS_CTL1_LTR_L12_TH_SCALE, scale);

	/* Some broken devices only support dword access to L1 SS */
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
		pci_clear_and_set_config_dword(child,
					       child->l1ss + PCI_L1SS_CTL1,
					       PCI_L1SS_CTL1_L1_2_MASK, 0);
		pci_clear_and_set_config_dword(parent,
					       parent->l1ss + PCI_L1SS_CTL1,
					       PCI_L1SS_CTL1_L1_2_MASK, 0);
	}

	/* Program T_POWER_ON times in both ports */
	pci_write_config_dword(parent, parent->l1ss + PCI_L1SS_CTL2, ctl2);
	pci_write_config_dword(child, child->l1ss + PCI_L1SS_CTL2, ctl2);

	/* Program Common_Mode_Restore_Time in upstream device */
	pci_clear_and_set_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1,
				       PCI_L1SS_CTL1_CM_RESTORE_TIME, ctl1);

	/* Program LTR_L1.2_THRESHOLD time in both ports */
	pci_clear_and_set_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1,
				       PCI_L1SS_CTL1_LTR_L12_TH_VALUE |
				       PCI_L1SS_CTL1_LTR_L12_TH_SCALE,
				       ctl1);
	pci_clear_and_set_config_dword(child, child->l1ss + PCI_L1SS_CTL1,
				       PCI_L1SS_CTL1_LTR_L12_TH_VALUE |
				       PCI_L1SS_CTL1_LTR_L12_TH_SCALE,
				       ctl1);

	if (pl1_2_enables || cl1_2_enables) {
		pci_clear_and_set_config_dword(parent,
					       parent->l1ss + PCI_L1SS_CTL1, 0,
					       pl1_2_enables);
		pci_clear_and_set_config_dword(child,
					       child->l1ss + PCI_L1SS_CTL1, 0,
					       cl1_2_enables);
	}
}

static void aspm_l1ss_init(struct pcie_link_state *link)
{
	struct pci_dev *child = link->downstream, *parent = link->pdev;
	u32 parent_l1ss_cap, child_l1ss_cap;
	u32 parent_l1ss_ctl1 = 0, child_l1ss_ctl1 = 0;

	if (!parent->l1ss || !child->l1ss)
		return;

	/* Setup L1 substate */
	pci_read_config_dword(parent, parent->l1ss + PCI_L1SS_CAP,
			      &parent_l1ss_cap);
	pci_read_config_dword(child, child->l1ss + PCI_L1SS_CAP,
			      &child_l1ss_cap);

	if (!(parent_l1ss_cap & PCI_L1SS_CAP_L1_PM_SS))
		parent_l1ss_cap = 0;
	if (!(child_l1ss_cap & PCI_L1SS_CAP_L1_PM_SS))
		child_l1ss_cap = 0;

	/*
	 * If we don't have LTR for the entire path from the Root Complex
	 * to this device, we can't use ASPM L1.2 because it relies on the
	 * LTR_L1.2_THRESHOLD.  See PCIe r4.0, secs 5.5.4, 6.18.
	 */
	if (!child->ltr_path)
		child_l1ss_cap &= ~PCI_L1SS_CAP_ASPM_L1_2;

	if (parent_l1ss_cap & child_l1ss_cap & PCI_L1SS_CAP_ASPM_L1_1)
		link->aspm_support |= ASPM_STATE_L1_1;
	if (parent_l1ss_cap & child_l1ss_cap & PCI_L1SS_CAP_ASPM_L1_2)
		link->aspm_support |= ASPM_STATE_L1_2;
	if (parent_l1ss_cap & child_l1ss_cap & PCI_L1SS_CAP_PCIPM_L1_1)
		link->aspm_support |= ASPM_STATE_L1_1_PCIPM;
	if (parent_l1ss_cap & child_l1ss_cap & PCI_L1SS_CAP_PCIPM_L1_2)
		link->aspm_support |= ASPM_STATE_L1_2_PCIPM;

	if (parent_l1ss_cap)
		pci_read_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1,
				      &parent_l1ss_ctl1);
	if (child_l1ss_cap)
		pci_read_config_dword(child, child->l1ss + PCI_L1SS_CTL1,
				      &child_l1ss_ctl1);

	if (parent_l1ss_ctl1 & child_l1ss_ctl1 & PCI_L1SS_CTL1_ASPM_L1_1)
		link->aspm_enabled |= ASPM_STATE_L1_1;
	if (parent_l1ss_ctl1 & child_l1ss_ctl1 & PCI_L1SS_CTL1_ASPM_L1_2)
		link->aspm_enabled |= ASPM_STATE_L1_2;
	if (parent_l1ss_ctl1 & child_l1ss_ctl1 & PCI_L1SS_CTL1_PCIPM_L1_1)
		link->aspm_enabled |= ASPM_STATE_L1_1_PCIPM;
	if (parent_l1ss_ctl1 & child_l1ss_ctl1 & PCI_L1SS_CTL1_PCIPM_L1_2)
		link->aspm_enabled |= ASPM_STATE_L1_2_PCIPM;

	if (link->aspm_support & ASPM_STATE_L1_2_MASK)
		aspm_calc_l12_info(link, parent_l1ss_cap, child_l1ss_cap);
}

static void pcie_aspm_cap_init(struct pcie_link_state *link, int blacklist)
{
	struct pci_dev *child = link->downstream, *parent = link->pdev;
	u32 parent_lnkcap, child_lnkcap;
	u16 parent_lnkctl, child_lnkctl;
	struct pci_bus *linkbus = parent->subordinate;

	if (blacklist) {
		/* Set enabled/disable so that we will disable ASPM later */
		link->aspm_enabled = ASPM_STATE_ALL;
		link->aspm_disable = ASPM_STATE_ALL;
		return;
	}

	/*
	 * If ASPM not supported, don't mess with the clocks and link,
	 * bail out now.
	 */
	pcie_capability_read_dword(parent, PCI_EXP_LNKCAP, &parent_lnkcap);
	pcie_capability_read_dword(child, PCI_EXP_LNKCAP, &child_lnkcap);
	if (!(parent_lnkcap & child_lnkcap & PCI_EXP_LNKCAP_ASPMS))
		return;

	/* Configure common clock before checking latencies */
	pcie_aspm_configure_common_clock(link);

	/*
	 * Re-read upstream/downstream components' register state after
	 * clock configuration.  L0s & L1 exit latencies in the otherwise
	 * read-only Link Capabilities may change depending on common clock
	 * configuration (PCIe r5.0, sec 7.5.3.6).
	 */
	pcie_capability_read_dword(parent, PCI_EXP_LNKCAP, &parent_lnkcap);
	pcie_capability_read_dword(child, PCI_EXP_LNKCAP, &child_lnkcap);
	pcie_capability_read_word(parent, PCI_EXP_LNKCTL, &parent_lnkctl);
	pcie_capability_read_word(child, PCI_EXP_LNKCTL, &child_lnkctl);

	/*
	 * Setup L0s state
	 *
	 * Note that we must not enable L0s in either direction on a
	 * given link unless components on both sides of the link each
	 * support L0s.
	 */
	if (parent_lnkcap & child_lnkcap & PCI_EXP_LNKCAP_ASPM_L0S)
		link->aspm_support |= ASPM_STATE_L0S;

	if (child_lnkctl & PCI_EXP_LNKCTL_ASPM_L0S)
		link->aspm_enabled |= ASPM_STATE_L0S_UP;
	if (parent_lnkctl & PCI_EXP_LNKCTL_ASPM_L0S)
		link->aspm_enabled |= ASPM_STATE_L0S_DW;

	/* Setup L1 state */
	if (parent_lnkcap & child_lnkcap & PCI_EXP_LNKCAP_ASPM_L1)
		link->aspm_support |= ASPM_STATE_L1;

	if (parent_lnkctl & child_lnkctl & PCI_EXP_LNKCTL_ASPM_L1)
		link->aspm_enabled |= ASPM_STATE_L1;

	aspm_l1ss_init(link);

	/* Save default state */
	link->aspm_default = link->aspm_enabled;

	/* Setup initial capable state. Will be updated later */
	link->aspm_capable = link->aspm_support;

	/* Get and check endpoint acceptable latencies */
	list_for_each_entry(child, &linkbus->devices, bus_list) {
		if (pci_pcie_type(child) != PCI_EXP_TYPE_ENDPOINT &&
		    pci_pcie_type(child) != PCI_EXP_TYPE_LEG_END)
			continue;

		pcie_aspm_check_latency(child);
	}
}

/* Configure the ASPM L1 substates */
static void pcie_config_aspm_l1ss(struct pcie_link_state *link, u32 state)
{
	u32 val, enable_req;
	struct pci_dev *child = link->downstream, *parent = link->pdev;

	enable_req = (link->aspm_enabled ^ state) & state;

	/*
	 * Here are the rules specified in the PCIe spec for enabling L1SS:
	 * - When enabling L1.x, enable bit at parent first, then at child
	 * - When disabling L1.x, disable bit at child first, then at parent
	 * - When enabling ASPM L1.x, need to disable L1
	 *   (at child followed by parent).
	 * - The ASPM/PCIPM L1.2 must be disabled while programming timing
	 *   parameters
	 *
	 * To keep it simple, disable all L1SS bits first, and later enable
	 * what is needed.
	 */

	/* Disable all L1 substates */
	pci_clear_and_set_config_dword(child, child->l1ss + PCI_L1SS_CTL1,
				       PCI_L1SS_CTL1_L1SS_MASK, 0);
	pci_clear_and_set_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1,
				       PCI_L1SS_CTL1_L1SS_MASK, 0);
	/*
	 * If needed, disable L1, and it gets enabled later
	 * in pcie_config_aspm_link().
	 */
	if (enable_req & (ASPM_STATE_L1_1 | ASPM_STATE_L1_2)) {
		pcie_capability_clear_word(child, PCI_EXP_LNKCTL,
					   PCI_EXP_LNKCTL_ASPM_L1);
		pcie_capability_clear_word(parent, PCI_EXP_LNKCTL,
					   PCI_EXP_LNKCTL_ASPM_L1);
	}

	val = 0;
	if (state & ASPM_STATE_L1_1)
		val |= PCI_L1SS_CTL1_ASPM_L1_1;
	if (state & ASPM_STATE_L1_2)
		val |= PCI_L1SS_CTL1_ASPM_L1_2;
	if (state & ASPM_STATE_L1_1_PCIPM)
		val |= PCI_L1SS_CTL1_PCIPM_L1_1;
	if (state & ASPM_STATE_L1_2_PCIPM)
		val |= PCI_L1SS_CTL1_PCIPM_L1_2;

	/* Enable what we need to enable */
	pci_clear_and_set_config_dword(parent, parent->l1ss + PCI_L1SS_CTL1,
				       PCI_L1SS_CTL1_L1SS_MASK, val);
	pci_clear_and_set_config_dword(child, child->l1ss + PCI_L1SS_CTL1,
				       PCI_L1SS_CTL1_L1SS_MASK, val);
}

static void pcie_config_aspm_dev(struct pci_dev *pdev, u32 val)
{
	pcie_capability_clear_and_set_word(pdev, PCI_EXP_LNKCTL,
					   PCI_EXP_LNKCTL_ASPMC, val);
}

static void pcie_config_aspm_link(struct pcie_link_state *link, u32 state)
{
	u32 upstream = 0, dwstream = 0;
	struct pci_dev *child = link->downstream, *parent = link->pdev;
	struct pci_bus *linkbus = parent->subordinate;

	/* Enable only the states that were not explicitly disabled */
	state &= (link->aspm_capable & ~link->aspm_disable);

	/* Can't enable any substates if L1 is not enabled */
	if (!(state & ASPM_STATE_L1))
		state &= ~ASPM_STATE_L1SS;

	/* Spec says both ports must be in D0 before enabling PCI PM substates*/
	if (parent->current_state != PCI_D0 || child->current_state != PCI_D0) {
		state &= ~ASPM_STATE_L1_SS_PCIPM;
		state |= (link->aspm_enabled & ASPM_STATE_L1_SS_PCIPM);
	}

	/* Nothing to do if the link is already in the requested state */
	if (link->aspm_enabled == state)
		return;
	/* Convert ASPM state to upstream/downstream ASPM register state */
	if (state & ASPM_STATE_L0S_UP)
		dwstream |= PCI_EXP_LNKCTL_ASPM_L0S;
	if (state & ASPM_STATE_L0S_DW)
		upstream |= PCI_EXP_LNKCTL_ASPM_L0S;
	if (state & ASPM_STATE_L1) {
		upstream |= PCI_EXP_LNKCTL_ASPM_L1;
		dwstream |= PCI_EXP_LNKCTL_ASPM_L1;
	}

	if (link->aspm_capable & ASPM_STATE_L1SS)
		pcie_config_aspm_l1ss(link, state);

	/*
	 * Spec 2.0 suggests all functions should be configured the
	 * same setting for ASPM. Enabling ASPM L1 should be done in
	 * upstream component first and then downstream, and vice
	 * versa for disabling ASPM L1. Spec doesn't mention L0S.
	 */
	if (state & ASPM_STATE_L1)
		pcie_config_aspm_dev(parent, upstream);
	list_for_each_entry(child, &linkbus->devices, bus_list)
		pcie_config_aspm_dev(child, dwstream);
	if (!(state & ASPM_STATE_L1))
		pcie_config_aspm_dev(parent, upstream);

	link->aspm_enabled = state;

	/* Update latest ASPM configuration in saved context */
	pci_save_aspm_l1ss_state(link->downstream);
	pci_update_aspm_saved_state(link->downstream);
	pci_save_aspm_l1ss_state(parent);
	pci_update_aspm_saved_state(parent);
}

static void pcie_config_aspm_path(struct pcie_link_state *link)
{
	while (link) {
		pcie_config_aspm_link(link, policy_to_aspm_state(link));
		link = link->parent;
	}
}

static void free_link_state(struct pcie_link_state *link)
{
	link->pdev->link_state = NULL;
	kfree(link);
}

static int pcie_aspm_sanity_check(struct pci_dev *pdev)
{
	struct pci_dev *child;
	u32 reg32;

	/*
	 * Some functions in a slot might not all be PCIe functions,
	 * very strange. Disable ASPM for the whole slot
	 */
	list_for_each_entry(child, &pdev->subordinate->devices, bus_list) {
		if (!pci_is_pcie(child))
			return -EINVAL;

		/*
		 * If ASPM is disabled then we're not going to change
		 * the BIOS state. It's safe to continue even if it's a
		 * pre-1.1 device
		 */

		if (aspm_disabled)
			continue;

		/*
		 * Disable ASPM for pre-1.1 PCIe device, we follow MS to use
		 * RBER bit to determine if a function is 1.1 version device
		 */
		pcie_capability_read_dword(child, PCI_EXP_DEVCAP, &reg32);
		if (!(reg32 & PCI_EXP_DEVCAP_RBER) && !aspm_force) {
			pci_info(child, "disabling ASPM on pre-1.1 PCIe device.  You can enable it with 'pcie_aspm=force'\n");
			return -EINVAL;
		}
	}
	return 0;
}

static struct pcie_link_state *alloc_pcie_link_state(struct pci_dev *pdev)
{
	struct pcie_link_state *link;

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return NULL;

	INIT_LIST_HEAD(&link->sibling);
	link->pdev = pdev;
	link->downstream = pci_function_0(pdev->subordinate);

	/*
	 * Root Ports and PCI/PCI-X to PCIe Bridges are roots of PCIe
	 * hierarchies.  Note that some PCIe host implementations omit
	 * the root ports entirely, in which case a downstream port on
	 * a switch may become the root of the link state chain for all
	 * its subordinate endpoints.
	 */
	if (pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT ||
	    pci_pcie_type(pdev) == PCI_EXP_TYPE_PCIE_BRIDGE ||
	    !pdev->bus->parent->self) {
		link->root = link;
	} else {
		struct pcie_link_state *parent;

		parent = pdev->bus->parent->self->link_state;
		if (!parent) {
			kfree(link);
			return NULL;
		}

		link->parent = parent;
		link->root = link->parent->root;
	}

	list_add(&link->sibling, &link_list);
	pdev->link_state = link;
	return link;
}

static void pcie_aspm_update_sysfs_visibility(struct pci_dev *pdev)
{
	struct pci_dev *child;

	list_for_each_entry(child, &pdev->subordinate->devices, bus_list)
		sysfs_update_group(&child->dev.kobj, &aspm_ctrl_attr_group);
}

/*
 * pcie_aspm_init_link_state: Initiate PCI express link state.
 * It is called after the pcie and its children devices are scanned.
 * @pdev: the root port or switch downstream port
 */
void pcie_aspm_init_link_state(struct pci_dev *pdev)
{
	struct pcie_link_state *link;
	int blacklist = !!pcie_aspm_sanity_check(pdev);

	if (!aspm_support_enabled)
		return;

	if (pdev->link_state)
		return;

	/*
	 * We allocate pcie_link_state for the component on the upstream
	 * end of a Link, so there's nothing to do unless this device is
	 * downstream port.
	 */
	if (!pcie_downstream_port(pdev))
		return;

	/* VIA has a strange chipset, root port is under a bridge */
	if (pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT &&
	    pdev->bus->self)
		return;

	down_read(&pci_bus_sem);
	if (list_empty(&pdev->subordinate->devices))
		goto out;

	mutex_lock(&aspm_lock);
	link = alloc_pcie_link_state(pdev);
	if (!link)
		goto unlock;
	/*
	 * Setup initial ASPM state. Note that we need to configure
	 * upstream links also because capable state of them can be
	 * update through pcie_aspm_cap_init().
	 */
	pcie_aspm_cap_init(link, blacklist);

	/* Setup initial Clock PM state */
	pcie_clkpm_cap_init(link, blacklist);

	/*
	 * At this stage drivers haven't had an opportunity to change the
	 * link policy setting. Enabling ASPM on broken hardware can cripple
	 * it even before the driver has had a chance to disable ASPM, so
	 * default to a safe level right now. If we're enabling ASPM beyond
	 * the BIOS's expectation, we'll do so once pci_enable_device() is
	 * called.
	 */
	if (aspm_policy != POLICY_POWERSAVE &&
	    aspm_policy != POLICY_POWER_SUPERSAVE) {
		pcie_config_aspm_path(link);
		pcie_set_clkpm(link, policy_to_clkpm_state(link));
	}

	pcie_aspm_update_sysfs_visibility(pdev);

unlock:
	mutex_unlock(&aspm_lock);
out:
	up_read(&pci_bus_sem);
}

void pci_bridge_reconfigure_ltr(struct pci_dev *pdev)
{
	struct pci_dev *bridge;
	u32 ctl;

	bridge = pci_upstream_bridge(pdev);
	if (bridge && bridge->ltr_path) {
		pcie_capability_read_dword(bridge, PCI_EXP_DEVCTL2, &ctl);
		if (!(ctl & PCI_EXP_DEVCTL2_LTR_EN)) {
			pci_dbg(bridge, "re-enabling LTR\n");
			pcie_capability_set_word(bridge, PCI_EXP_DEVCTL2,
						 PCI_EXP_DEVCTL2_LTR_EN);
		}
	}
}

void pci_configure_ltr(struct pci_dev *pdev)
{
	struct pci_host_bridge *host = pci_find_host_bridge(pdev->bus);
	struct pci_dev *bridge;
	u32 cap, ctl;

	if (!pci_is_pcie(pdev))
		return;

	pcie_capability_read_dword(pdev, PCI_EXP_DEVCAP2, &cap);
	if (!(cap & PCI_EXP_DEVCAP2_LTR))
		return;

	pcie_capability_read_dword(pdev, PCI_EXP_DEVCTL2, &ctl);
	if (ctl & PCI_EXP_DEVCTL2_LTR_EN) {
		if (pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT) {
			pdev->ltr_path = 1;
			return;
		}

		bridge = pci_upstream_bridge(pdev);
		if (bridge && bridge->ltr_path)
			pdev->ltr_path = 1;

		return;
	}

	if (!host->native_ltr)
		return;

	/*
	 * Software must not enable LTR in an Endpoint unless the Root
	 * Complex and all intermediate Switches indicate support for LTR.
	 * PCIe r4.0, sec 6.18.
	 */
	if (pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT) {
		pcie_capability_set_word(pdev, PCI_EXP_DEVCTL2,
					 PCI_EXP_DEVCTL2_LTR_EN);
		pdev->ltr_path = 1;
		return;
	}

	/*
	 * If we're configuring a hot-added device, LTR was likely
	 * disabled in the upstream bridge, so re-enable it before enabling
	 * it in the new device.
	 */
	bridge = pci_upstream_bridge(pdev);
	if (bridge && bridge->ltr_path) {
		pci_bridge_reconfigure_ltr(pdev);
		pcie_capability_set_word(pdev, PCI_EXP_DEVCTL2,
					 PCI_EXP_DEVCTL2_LTR_EN);
		pdev->ltr_path = 1;
	}
}

/* Recheck latencies and update aspm_capable for links under the root */
static void pcie_update_aspm_capable(struct pcie_link_state *root)
{
	struct pcie_link_state *link;
	BUG_ON(root->parent);
	list_for_each_entry(link, &link_list, sibling) {
		if (link->root != root)
			continue;
		link->aspm_capable = link->aspm_support;
	}
	list_for_each_entry(link, &link_list, sibling) {
		struct pci_dev *child;
		struct pci_bus *linkbus = link->pdev->subordinate;
		if (link->root != root)
			continue;
		list_for_each_entry(child, &linkbus->devices, bus_list) {
			if ((pci_pcie_type(child) != PCI_EXP_TYPE_ENDPOINT) &&
			    (pci_pcie_type(child) != PCI_EXP_TYPE_LEG_END))
				continue;
			pcie_aspm_check_latency(child);
		}
	}
}

/* @pdev: the endpoint device */
void pcie_aspm_exit_link_state(struct pci_dev *pdev)
{
	struct pci_dev *parent = pdev->bus->self;
	struct pcie_link_state *link, *root, *parent_link;

	if (!parent || !parent->link_state)
		return;

	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);

	link = parent->link_state;
	root = link->root;
	parent_link = link->parent;

	/*
	 * link->downstream is a pointer to the pci_dev of function 0.  If
	 * we remove that function, the pci_dev is about to be deallocated,
	 * so we can't use link->downstream again.  Free the link state to
	 * avoid this.
	 *
	 * If we're removing a non-0 function, it's possible we could
	 * retain the link state, but PCIe r6.0, sec 7.5.3.7, recommends
	 * programming the same ASPM Control value for all functions of
	 * multi-function devices, so disable ASPM for all of them.
	 */
	pcie_config_aspm_link(link, 0);
	list_del(&link->sibling);
	free_link_state(link);

	/* Recheck latencies and configure upstream links */
	if (parent_link) {
		pcie_update_aspm_capable(root);
		pcie_config_aspm_path(parent_link);
	}

	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);
}

/*
 * @pdev: the root port or switch downstream port
 * @locked: whether pci_bus_sem is held
 */
void pcie_aspm_pm_state_change(struct pci_dev *pdev, bool locked)
{
	struct pcie_link_state *link = pdev->link_state;

	if (aspm_disabled || !link)
		return;
	/*
	 * Devices changed PM state, we should recheck if latency
	 * meets all functions' requirement
	 */
	if (!locked)
		down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	pcie_update_aspm_capable(link->root);
	pcie_config_aspm_path(link);
	mutex_unlock(&aspm_lock);
	if (!locked)
		up_read(&pci_bus_sem);
}

void pcie_aspm_powersave_config_link(struct pci_dev *pdev)
{
	struct pcie_link_state *link = pdev->link_state;

	if (aspm_disabled || !link)
		return;

	if (aspm_policy != POLICY_POWERSAVE &&
	    aspm_policy != POLICY_POWER_SUPERSAVE)
		return;

	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	pcie_config_aspm_path(link);
	pcie_set_clkpm(link, policy_to_clkpm_state(link));
	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);
}

static struct pcie_link_state *pcie_aspm_get_link(struct pci_dev *pdev)
{
	struct pci_dev *bridge;

	if (!pci_is_pcie(pdev))
		return NULL;

	bridge = pci_upstream_bridge(pdev);
	if (!bridge || !pci_is_pcie(bridge))
		return NULL;

	return bridge->link_state;
}

static int __pci_disable_link_state(struct pci_dev *pdev, int state, bool locked)
{
	struct pcie_link_state *link = pcie_aspm_get_link(pdev);

	if (!link)
		return -EINVAL;
	/*
	 * A driver requested that ASPM be disabled on this device, but
	 * if we don't have permission to manage ASPM (e.g., on ACPI
	 * systems we have to observe the FADT ACPI_FADT_NO_ASPM bit and
	 * the _OSC method), we can't honor that request.  Windows has
	 * a similar mechanism using "PciASPMOptOut", which is also
	 * ignored in this situation.
	 */
	if (aspm_disabled) {
		pci_warn(pdev, "can't disable ASPM; OS doesn't have ASPM control\n");
		return -EPERM;
	}

	if (!locked)
		down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	if (state & PCIE_LINK_STATE_L0S)
		link->aspm_disable |= ASPM_STATE_L0S;
	if (state & PCIE_LINK_STATE_L1)
		/* L1 PM substates require L1 */
		link->aspm_disable |= ASPM_STATE_L1 | ASPM_STATE_L1SS;
	if (state & PCIE_LINK_STATE_L1_1)
		link->aspm_disable |= ASPM_STATE_L1_1;
	if (state & PCIE_LINK_STATE_L1_2)
		link->aspm_disable |= ASPM_STATE_L1_2;
	if (state & PCIE_LINK_STATE_L1_1_PCIPM)
		link->aspm_disable |= ASPM_STATE_L1_1_PCIPM;
	if (state & PCIE_LINK_STATE_L1_2_PCIPM)
		link->aspm_disable |= ASPM_STATE_L1_2_PCIPM;
	pcie_config_aspm_link(link, policy_to_aspm_state(link));

	if (state & PCIE_LINK_STATE_CLKPM)
		link->clkpm_disable = 1;
	pcie_set_clkpm(link, policy_to_clkpm_state(link));
	mutex_unlock(&aspm_lock);
	if (!locked)
		up_read(&pci_bus_sem);

	return 0;
}

int pci_disable_link_state_locked(struct pci_dev *pdev, int state)
{
	lockdep_assert_held_read(&pci_bus_sem);

	return __pci_disable_link_state(pdev, state, true);
}
EXPORT_SYMBOL(pci_disable_link_state_locked);

/**
 * pci_disable_link_state - Disable device's link state, so the link will
 * never enter specific states.  Note that if the BIOS didn't grant ASPM
 * control to the OS, this does nothing because we can't touch the LNKCTL
 * register. Returns 0 or a negative errno.
 *
 * @pdev: PCI device
 * @state: ASPM link state to disable
 */
int pci_disable_link_state(struct pci_dev *pdev, int state)
{
	return __pci_disable_link_state(pdev, state, false);
}
EXPORT_SYMBOL(pci_disable_link_state);

static int __pci_enable_link_state(struct pci_dev *pdev, int state, bool locked)
{
	struct pcie_link_state *link = pcie_aspm_get_link(pdev);

	if (!link)
		return -EINVAL;
	/*
	 * A driver requested that ASPM be enabled on this device, but
	 * if we don't have permission to manage ASPM (e.g., on ACPI
	 * systems we have to observe the FADT ACPI_FADT_NO_ASPM bit and
	 * the _OSC method), we can't honor that request.
	 */
	if (aspm_disabled) {
		pci_warn(pdev, "can't override BIOS ASPM; OS doesn't have ASPM control\n");
		return -EPERM;
	}

	if (!locked)
		down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	link->aspm_default = 0;
	if (state & PCIE_LINK_STATE_L0S)
		link->aspm_default |= ASPM_STATE_L0S;
	if (state & PCIE_LINK_STATE_L1)
		link->aspm_default |= ASPM_STATE_L1;
	/* L1 PM substates require L1 */
	if (state & PCIE_LINK_STATE_L1_1)
		link->aspm_default |= ASPM_STATE_L1_1 | ASPM_STATE_L1;
	if (state & PCIE_LINK_STATE_L1_2)
		link->aspm_default |= ASPM_STATE_L1_2 | ASPM_STATE_L1;
	if (state & PCIE_LINK_STATE_L1_1_PCIPM)
		link->aspm_default |= ASPM_STATE_L1_1_PCIPM | ASPM_STATE_L1;
	if (state & PCIE_LINK_STATE_L1_2_PCIPM)
		link->aspm_default |= ASPM_STATE_L1_2_PCIPM | ASPM_STATE_L1;
	pcie_config_aspm_link(link, policy_to_aspm_state(link));

	link->clkpm_default = (state & PCIE_LINK_STATE_CLKPM) ? 1 : 0;
	pcie_set_clkpm(link, policy_to_clkpm_state(link));
	mutex_unlock(&aspm_lock);
	if (!locked)
		up_read(&pci_bus_sem);

	return 0;
}

/**
 * pci_enable_link_state - Clear and set the default device link state so that
 * the link may be allowed to enter the specified states. Note that if the
 * BIOS didn't grant ASPM control to the OS, this does nothing because we can't
 * touch the LNKCTL register. Also note that this does not enable states
 * disabled by pci_disable_link_state(). Return 0 or a negative errno.
 *
 * @pdev: PCI device
 * @state: Mask of ASPM link states to enable
 */
int pci_enable_link_state(struct pci_dev *pdev, int state)
{
	return __pci_enable_link_state(pdev, state, false);
}
EXPORT_SYMBOL(pci_enable_link_state);

/**
 * pci_enable_link_state_locked - Clear and set the default device link state
 * so that the link may be allowed to enter the specified states. Note that if
 * the BIOS didn't grant ASPM control to the OS, this does nothing because we
 * can't touch the LNKCTL register. Also note that this does not enable states
 * disabled by pci_disable_link_state(). Return 0 or a negative errno.
 *
 * @pdev: PCI device
 * @state: Mask of ASPM link states to enable
 *
 * Context: Caller holds pci_bus_sem read lock.
 */
int pci_enable_link_state_locked(struct pci_dev *pdev, int state)
{
	lockdep_assert_held_read(&pci_bus_sem);

	return __pci_enable_link_state(pdev, state, true);
}
EXPORT_SYMBOL(pci_enable_link_state_locked);

static int pcie_aspm_set_policy(const char *val,
				const struct kernel_param *kp)
{
	int i;
	struct pcie_link_state *link;

	if (aspm_disabled)
		return -EPERM;
	i = sysfs_match_string(policy_str, val);
	if (i < 0)
		return i;
	if (i == aspm_policy)
		return 0;

	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	aspm_policy = i;
	list_for_each_entry(link, &link_list, sibling) {
		pcie_config_aspm_link(link, policy_to_aspm_state(link));
		pcie_set_clkpm(link, policy_to_clkpm_state(link));
	}
	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);
	return 0;
}

static int pcie_aspm_get_policy(char *buffer, const struct kernel_param *kp)
{
	int i, cnt = 0;
	for (i = 0; i < ARRAY_SIZE(policy_str); i++)
		if (i == aspm_policy)
			cnt += sprintf(buffer + cnt, "[%s] ", policy_str[i]);
		else
			cnt += sprintf(buffer + cnt, "%s ", policy_str[i]);
	cnt += sprintf(buffer + cnt, "\n");
	return cnt;
}

module_param_call(policy, pcie_aspm_set_policy, pcie_aspm_get_policy,
	NULL, 0644);

/**
 * pcie_aspm_enabled - Check if PCIe ASPM has been enabled for a device.
 * @pdev: Target device.
 *
 * Relies on the upstream bridge's link_state being valid.  The link_state
 * is deallocated only when the last child of the bridge (i.e., @pdev or a
 * sibling) is removed, and the caller should be holding a reference to
 * @pdev, so this should be safe.
 */
bool pcie_aspm_enabled(struct pci_dev *pdev)
{
	struct pcie_link_state *link = pcie_aspm_get_link(pdev);

	if (!link)
		return false;

	return link->aspm_enabled;
}
EXPORT_SYMBOL_GPL(pcie_aspm_enabled);

static ssize_t aspm_attr_show_common(struct device *dev,
				     struct device_attribute *attr,
				     char *buf, u8 state)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pcie_link_state *link = pcie_aspm_get_link(pdev);

	return sysfs_emit(buf, "%d\n", (link->aspm_enabled & state) ? 1 : 0);
}

static ssize_t aspm_attr_store_common(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len, u8 state)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pcie_link_state *link = pcie_aspm_get_link(pdev);
	bool state_enable;

	if (kstrtobool(buf, &state_enable) < 0)
		return -EINVAL;

	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);

	if (state_enable) {
		link->aspm_disable &= ~state;
		/* need to enable L1 for substates */
		if (state & ASPM_STATE_L1SS)
			link->aspm_disable &= ~ASPM_STATE_L1;
	} else {
		link->aspm_disable |= state;
		if (state & ASPM_STATE_L1)
			link->aspm_disable |= ASPM_STATE_L1SS;
	}

	pcie_config_aspm_link(link, policy_to_aspm_state(link));

	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);

	return len;
}

#define ASPM_ATTR(_f, _s)						\
static ssize_t _f##_show(struct device *dev,				\
			 struct device_attribute *attr, char *buf)	\
{ return aspm_attr_show_common(dev, attr, buf, ASPM_STATE_##_s); }	\
									\
static ssize_t _f##_store(struct device *dev,				\
			  struct device_attribute *attr,		\
			  const char *buf, size_t len)			\
{ return aspm_attr_store_common(dev, attr, buf, len, ASPM_STATE_##_s); }

ASPM_ATTR(l0s_aspm, L0S)
ASPM_ATTR(l1_aspm, L1)
ASPM_ATTR(l1_1_aspm, L1_1)
ASPM_ATTR(l1_2_aspm, L1_2)
ASPM_ATTR(l1_1_pcipm, L1_1_PCIPM)
ASPM_ATTR(l1_2_pcipm, L1_2_PCIPM)

static ssize_t clkpm_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pcie_link_state *link = pcie_aspm_get_link(pdev);

	return sysfs_emit(buf, "%d\n", link->clkpm_enabled);
}

static ssize_t clkpm_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t len)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pcie_link_state *link = pcie_aspm_get_link(pdev);
	bool state_enable;

	if (kstrtobool(buf, &state_enable) < 0)
		return -EINVAL;

	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);

	link->clkpm_disable = !state_enable;
	pcie_set_clkpm(link, policy_to_clkpm_state(link));

	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);

	return len;
}

static DEVICE_ATTR_RW(clkpm);
static DEVICE_ATTR_RW(l0s_aspm);
static DEVICE_ATTR_RW(l1_aspm);
static DEVICE_ATTR_RW(l1_1_aspm);
static DEVICE_ATTR_RW(l1_2_aspm);
static DEVICE_ATTR_RW(l1_1_pcipm);
static DEVICE_ATTR_RW(l1_2_pcipm);

static struct attribute *aspm_ctrl_attrs[] = {
	&dev_attr_clkpm.attr,
	&dev_attr_l0s_aspm.attr,
	&dev_attr_l1_aspm.attr,
	&dev_attr_l1_1_aspm.attr,
	&dev_attr_l1_2_aspm.attr,
	&dev_attr_l1_1_pcipm.attr,
	&dev_attr_l1_2_pcipm.attr,
	NULL
};

static umode_t aspm_ctrl_attrs_are_visible(struct kobject *kobj,
					   struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pcie_link_state *link = pcie_aspm_get_link(pdev);
	static const u8 aspm_state_map[] = {
		ASPM_STATE_L0S,
		ASPM_STATE_L1,
		ASPM_STATE_L1_1,
		ASPM_STATE_L1_2,
		ASPM_STATE_L1_1_PCIPM,
		ASPM_STATE_L1_2_PCIPM,
	};

	if (aspm_disabled || !link)
		return 0;

	if (n == 0)
		return link->clkpm_capable ? a->mode : 0;

	return link->aspm_capable & aspm_state_map[n - 1] ? a->mode : 0;
}

const struct attribute_group aspm_ctrl_attr_group = {
	.name = "link",
	.attrs = aspm_ctrl_attrs,
	.is_visible = aspm_ctrl_attrs_are_visible,
};

static int __init pcie_aspm_disable(char *str)
{
	if (!strcmp(str, "off")) {
		aspm_policy = POLICY_DEFAULT;
		aspm_disabled = 1;
		aspm_support_enabled = false;
		pr_info("PCIe ASPM is disabled\n");
	} else if (!strcmp(str, "force")) {
		aspm_force = 1;
		pr_info("PCIe ASPM is forcibly enabled\n");
	}
	return 1;
}

__setup("pcie_aspm=", pcie_aspm_disable);

void pcie_no_aspm(void)
{
	/*
	 * Disabling ASPM is intended to prevent the kernel from modifying
	 * existing hardware state, not to clear existing state. To that end:
	 * (a) set policy to POLICY_DEFAULT in order to avoid changing state
	 * (b) prevent userspace from changing policy
	 */
	if (!aspm_force) {
		aspm_policy = POLICY_DEFAULT;
		aspm_disabled = 1;
	}
}

bool pcie_aspm_support_enabled(void)
{
	return aspm_support_enabled;
}

#endif /* CONFIG_PCIEASPM */
