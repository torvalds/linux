/*
 * File:	drivers/pci/pcie/aspm.c
 * Enabling PCIE link L0s/L1 state and Clock Power Management
 *
 * Copyright (C) 2007 Intel
 * Copyright (C) Zhang Yanmin (yanmin.zhang@intel.com)
 * Copyright (C) Shaohua Li (shaohua.li@intel.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/pci-aspm.h>
#include "../pci.h"

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "pcie_aspm."

struct aspm_latency {
	u32 l0s;			/* L0s latency (nsec) */
	u32 l1;				/* L1 latency (nsec) */
};

struct pcie_link_state {
	struct pci_dev *pdev;		/* Upstream component of the Link */
	struct pcie_link_state *root;	/* pointer to the root port link */
	struct pcie_link_state *parent;	/* pointer to the parent Link state */
	struct list_head sibling;	/* node in link_list */
	struct list_head children;	/* list of child link states */
	struct list_head link;		/* node in parent's children list */

	/* ASPM state */
	u32 aspm_support:2;		/* Supported ASPM state */
	u32 aspm_enabled:2;		/* Enabled ASPM state */
	u32 aspm_default:2;		/* Default ASPM state by BIOS */

	/* Clock PM state */
	u32 clkpm_capable:1;		/* Clock PM capable? */
	u32 clkpm_enabled:1;		/* Current Clock PM state */
	u32 clkpm_default:1;		/* Default Clock PM state by BIOS */

	/* Latencies */
	struct aspm_latency latency;	/* Exit latency */
	/*
	 * Endpoint acceptable latencies. A pcie downstream port only
	 * has one slot under it, so at most there are 8 functions.
	 */
	struct aspm_latency acceptable[8];
};

static int aspm_disabled, aspm_force;
static DEFINE_MUTEX(aspm_lock);
static LIST_HEAD(link_list);

#define POLICY_DEFAULT 0	/* BIOS default setting */
#define POLICY_PERFORMANCE 1	/* high performance */
#define POLICY_POWERSAVE 2	/* high power saving */
static int aspm_policy;
static const char *policy_str[] = {
	[POLICY_DEFAULT] = "default",
	[POLICY_PERFORMANCE] = "performance",
	[POLICY_POWERSAVE] = "powersave"
};

#define LINK_RETRAIN_TIMEOUT HZ

static int policy_to_aspm_state(struct pcie_link_state *link)
{
	switch (aspm_policy) {
	case POLICY_PERFORMANCE:
		/* Disable ASPM and Clock PM */
		return 0;
	case POLICY_POWERSAVE:
		/* Enable ASPM L0s/L1 */
		return PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1;
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
		/* Disable Clock PM */
		return 1;
	case POLICY_DEFAULT:
		return link->clkpm_default;
	}
	return 0;
}

static void pcie_set_clkpm_nocheck(struct pcie_link_state *link, int enable)
{
	int pos;
	u16 reg16;
	struct pci_dev *child;
	struct pci_bus *linkbus = link->pdev->subordinate;

	list_for_each_entry(child, &linkbus->devices, bus_list) {
		pos = pci_find_capability(child, PCI_CAP_ID_EXP);
		if (!pos)
			return;
		pci_read_config_word(child, pos + PCI_EXP_LNKCTL, &reg16);
		if (enable)
			reg16 |= PCI_EXP_LNKCTL_CLKREQ_EN;
		else
			reg16 &= ~PCI_EXP_LNKCTL_CLKREQ_EN;
		pci_write_config_word(child, pos + PCI_EXP_LNKCTL, reg16);
	}
	link->clkpm_enabled = !!enable;
}

static void pcie_set_clkpm(struct pcie_link_state *link, int enable)
{
	/* Don't enable Clock PM if the link is not Clock PM capable */
	if (!link->clkpm_capable && enable)
		return;
	/* Need nothing if the specified equals to current state */
	if (link->clkpm_enabled == enable)
		return;
	pcie_set_clkpm_nocheck(link, enable);
}

static void pcie_clkpm_cap_init(struct pcie_link_state *link, int blacklist)
{
	int pos, capable = 1, enabled = 1;
	u32 reg32;
	u16 reg16;
	struct pci_dev *child;
	struct pci_bus *linkbus = link->pdev->subordinate;

	/* All functions should have the same cap and state, take the worst */
	list_for_each_entry(child, &linkbus->devices, bus_list) {
		pos = pci_find_capability(child, PCI_CAP_ID_EXP);
		if (!pos)
			return;
		pci_read_config_dword(child, pos + PCI_EXP_LNKCAP, &reg32);
		if (!(reg32 & PCI_EXP_LNKCAP_CLKPM)) {
			capable = 0;
			enabled = 0;
			break;
		}
		pci_read_config_word(child, pos + PCI_EXP_LNKCTL, &reg16);
		if (!(reg16 & PCI_EXP_LNKCTL_CLKREQ_EN))
			enabled = 0;
	}
	link->clkpm_enabled = enabled;
	link->clkpm_default = enabled;
	link->clkpm_capable = (blacklist) ? 0 : capable;
}

static bool pcie_aspm_downstream_has_switch(struct pcie_link_state *link)
{
	struct pci_dev *child;
	struct pci_bus *linkbus = link->pdev->subordinate;

	list_for_each_entry(child, &linkbus->devices, bus_list) {
		if (child->pcie_type == PCI_EXP_TYPE_UPSTREAM)
			return true;
	}
	return false;
}

/*
 * pcie_aspm_configure_common_clock: check if the 2 ends of a link
 *   could use common clock. If they are, configure them to use the
 *   common clock. That will reduce the ASPM state exit latency.
 */
static void pcie_aspm_configure_common_clock(struct pcie_link_state *link)
{
	int ppos, cpos, same_clock = 1;
	u16 reg16, parent_reg, child_reg[8];
	unsigned long start_jiffies;
	struct pci_dev *child, *parent = link->pdev;
	struct pci_bus *linkbus = parent->subordinate;
	/*
	 * All functions of a slot should have the same Slot Clock
	 * Configuration, so just check one function
	 */
	child = list_entry(linkbus->devices.next, struct pci_dev, bus_list);
	BUG_ON(!child->is_pcie);

	/* Check downstream component if bit Slot Clock Configuration is 1 */
	cpos = pci_find_capability(child, PCI_CAP_ID_EXP);
	pci_read_config_word(child, cpos + PCI_EXP_LNKSTA, &reg16);
	if (!(reg16 & PCI_EXP_LNKSTA_SLC))
		same_clock = 0;

	/* Check upstream component if bit Slot Clock Configuration is 1 */
	ppos = pci_find_capability(parent, PCI_CAP_ID_EXP);
	pci_read_config_word(parent, ppos + PCI_EXP_LNKSTA, &reg16);
	if (!(reg16 & PCI_EXP_LNKSTA_SLC))
		same_clock = 0;

	/* Configure downstream component, all functions */
	list_for_each_entry(child, &linkbus->devices, bus_list) {
		cpos = pci_find_capability(child, PCI_CAP_ID_EXP);
		pci_read_config_word(child, cpos + PCI_EXP_LNKCTL, &reg16);
		child_reg[PCI_FUNC(child->devfn)] = reg16;
		if (same_clock)
			reg16 |= PCI_EXP_LNKCTL_CCC;
		else
			reg16 &= ~PCI_EXP_LNKCTL_CCC;
		pci_write_config_word(child, cpos + PCI_EXP_LNKCTL, reg16);
	}

	/* Configure upstream component */
	pci_read_config_word(parent, ppos + PCI_EXP_LNKCTL, &reg16);
	parent_reg = reg16;
	if (same_clock)
		reg16 |= PCI_EXP_LNKCTL_CCC;
	else
		reg16 &= ~PCI_EXP_LNKCTL_CCC;
	pci_write_config_word(parent, ppos + PCI_EXP_LNKCTL, reg16);

	/* Retrain link */
	reg16 |= PCI_EXP_LNKCTL_RL;
	pci_write_config_word(parent, ppos + PCI_EXP_LNKCTL, reg16);

	/* Wait for link training end. Break out after waiting for timeout */
	start_jiffies = jiffies;
	for (;;) {
		pci_read_config_word(parent, ppos + PCI_EXP_LNKSTA, &reg16);
		if (!(reg16 & PCI_EXP_LNKSTA_LT))
			break;
		if (time_after(jiffies, start_jiffies + LINK_RETRAIN_TIMEOUT))
			break;
		msleep(1);
	}
	if (!(reg16 & PCI_EXP_LNKSTA_LT))
		return;

	/* Training failed. Restore common clock configurations */
	dev_printk(KERN_ERR, &parent->dev,
		   "ASPM: Could not configure common clock\n");
	list_for_each_entry(child, &linkbus->devices, bus_list) {
		cpos = pci_find_capability(child, PCI_CAP_ID_EXP);
		pci_write_config_word(child, cpos + PCI_EXP_LNKCTL,
				      child_reg[PCI_FUNC(child->devfn)]);
	}
	pci_write_config_word(parent, ppos + PCI_EXP_LNKCTL, parent_reg);
}

/* Convert L0s latency encoding to ns */
static u32 calc_l0s_latency(u32 encoding)
{
	if (encoding == 0x7)
		return (5 * 1000);	/* > 4us */
	return (64 << encoding);
}

/* Convert L0s acceptable latency encoding to ns */
static u32 calc_l0s_acceptable(u32 encoding)
{
	if (encoding == 0x7)
		return -1U;
	return (64 << encoding);
}

/* Convert L1 latency encoding to ns */
static u32 calc_l1_latency(u32 encoding)
{
	if (encoding == 0x7)
		return (65 * 1000);	/* > 64us */
	return (1000 << encoding);
}

/* Convert L1 acceptable latency encoding to ns */
static u32 calc_l1_acceptable(u32 encoding)
{
	if (encoding == 0x7)
		return -1U;
	return (1000 << encoding);
}

static void pcie_aspm_get_cap_device(struct pci_dev *pdev, u32 *state,
				     u32 *l0s, u32 *l1, u32 *enabled)
{
	int pos;
	u16 reg16;
	u32 reg32, encoding;

	*l0s = *l1 = *enabled = 0;
	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	pci_read_config_dword(pdev, pos + PCI_EXP_LNKCAP, &reg32);
	*state = (reg32 & PCI_EXP_LNKCAP_ASPMS) >> 10;
	if (*state != PCIE_LINK_STATE_L0S &&
	    *state != (PCIE_LINK_STATE_L1 | PCIE_LINK_STATE_L0S))
		*state = 0;
	if (*state == 0)
		return;

	encoding = (reg32 & PCI_EXP_LNKCAP_L0SEL) >> 12;
	*l0s = calc_l0s_latency(encoding);
	if (*state & PCIE_LINK_STATE_L1) {
		encoding = (reg32 & PCI_EXP_LNKCAP_L1EL) >> 15;
		*l1 = calc_l1_latency(encoding);
	}
	pci_read_config_word(pdev, pos + PCI_EXP_LNKCTL, &reg16);
	*enabled = reg16 & (PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);
}

static void pcie_aspm_cap_init(struct pcie_link_state *link, int blacklist)
{
	u32 support, l0s, l1, enabled;
	struct pci_dev *child, *parent = link->pdev;
	struct pci_bus *linkbus = parent->subordinate;

	if (blacklist) {
		/* Set support state to 0, so we will disable ASPM later */
		link->aspm_support = 0;
		link->aspm_default = 0;
		link->aspm_enabled = PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1;
		return;
	}

	/* Configure common clock before checking latencies */
	pcie_aspm_configure_common_clock(link);

	/* upstream component states */
	pcie_aspm_get_cap_device(parent, &support, &l0s, &l1, &enabled);
	link->aspm_support = support;
	link->latency.l0s = l0s;
	link->latency.l1 = l1;
	link->aspm_enabled = enabled;

	/* downstream component states, all functions have the same setting */
	child = list_entry(linkbus->devices.next, struct pci_dev, bus_list);
	pcie_aspm_get_cap_device(child, &support, &l0s, &l1, &enabled);
	link->aspm_support &= support;
	link->latency.l0s = max_t(u32, link->latency.l0s, l0s);
	link->latency.l1 = max_t(u32, link->latency.l1, l1);

	if (!link->aspm_support)
		return;

	link->aspm_enabled &= link->aspm_support;
	link->aspm_default = link->aspm_enabled;

	/* ENDPOINT states*/
	list_for_each_entry(child, &linkbus->devices, bus_list) {
		int pos;
		u32 reg32, encoding;
		struct aspm_latency *acceptable =
			&link->acceptable[PCI_FUNC(child->devfn)];

		if (child->pcie_type != PCI_EXP_TYPE_ENDPOINT &&
		    child->pcie_type != PCI_EXP_TYPE_LEG_END)
			continue;

		pos = pci_find_capability(child, PCI_CAP_ID_EXP);
		pci_read_config_dword(child, pos + PCI_EXP_DEVCAP, &reg32);
		encoding = (reg32 & PCI_EXP_DEVCAP_L0S) >> 6;
		acceptable->l0s = calc_l0s_acceptable(encoding);
		if (link->aspm_support & PCIE_LINK_STATE_L1) {
			encoding = (reg32 & PCI_EXP_DEVCAP_L1) >> 9;
			acceptable->l1 = calc_l1_acceptable(encoding);
		}
	}
}

/**
 * __pcie_aspm_check_state_one - check latency for endpoint device.
 * @endpoint: pointer to the struct pci_dev of endpoint device
 *
 * TBD: The latency from the endpoint to root complex vary per switch's
 * upstream link state above the device. Here we just do a simple check
 * which assumes all links above the device can be in L1 state, that
 * is we just consider the worst case. If switch's upstream link can't
 * be put into L0S/L1, then our check is too strictly.
 */
static u32 __pcie_aspm_check_state_one(struct pci_dev *endpoint, u32 state)
{
	u32 l1_switch_latency = 0;
	struct aspm_latency *acceptable;
	struct pcie_link_state *link;

	link = endpoint->bus->self->link_state;
	state &= link->aspm_support;
	acceptable = &link->acceptable[PCI_FUNC(endpoint->devfn)];

	while (link && state) {
		if ((state & PCIE_LINK_STATE_L0S) &&
		    (link->latency.l0s > acceptable->l0s))
			state &= ~PCIE_LINK_STATE_L0S;
		if ((state & PCIE_LINK_STATE_L1) &&
		    (link->latency.l1 + l1_switch_latency > acceptable->l1))
			state &= ~PCIE_LINK_STATE_L1;
		link = link->parent;
		/*
		 * Every switch on the path to root complex need 1
		 * more microsecond for L1. Spec doesn't mention L0s.
		 */
		l1_switch_latency += 1000;
	}
	return state;
}

static u32 pcie_aspm_check_state(struct pcie_link_state *link, u32 state)
{
	pci_power_t power_state;
	struct pci_dev *child;
	struct pci_bus *linkbus = link->pdev->subordinate;

	/* If no child, ignore the link */
	if (list_empty(&linkbus->devices))
		return state;

	list_for_each_entry(child, &linkbus->devices, bus_list) {
		/*
		 * If downstream component of a link is pci bridge, we
		 * disable ASPM for now for the link
		 */
		if (child->pcie_type == PCI_EXP_TYPE_PCI_BRIDGE)
			return 0;

		if ((child->pcie_type != PCI_EXP_TYPE_ENDPOINT &&
		     child->pcie_type != PCI_EXP_TYPE_LEG_END))
			continue;
		/* Device not in D0 doesn't need check latency */
		power_state = child->current_state;
		if (power_state == PCI_D1 || power_state == PCI_D2 ||
		    power_state == PCI_D3hot || power_state == PCI_D3cold)
			continue;
		state = __pcie_aspm_check_state_one(child, state);
	}
	return state;
}

static void __pcie_aspm_config_one_dev(struct pci_dev *pdev, unsigned int state)
{
	u16 reg16;
	int pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);

	pci_read_config_word(pdev, pos + PCI_EXP_LNKCTL, &reg16);
	reg16 &= ~0x3;
	reg16 |= state;
	pci_write_config_word(pdev, pos + PCI_EXP_LNKCTL, reg16);
}

static void __pcie_aspm_config_link(struct pcie_link_state *link, u32 state)
{
	struct pci_dev *child, *parent = link->pdev;
	struct pci_bus *linkbus = parent->subordinate;

	/* If no child, disable the link */
	if (list_empty(&linkbus->devices))
		state = 0;
	/*
	 * If the downstream component has pci bridge function, don't
	 * do ASPM now.
	 */
	list_for_each_entry(child, &linkbus->devices, bus_list) {
		if (child->pcie_type == PCI_EXP_TYPE_PCI_BRIDGE)
			return;
	}
	/*
	 * Spec 2.0 suggests all functions should be configured the
	 * same setting for ASPM. Enabling ASPM L1 should be done in
	 * upstream component first and then downstream, and vice
	 * versa for disabling ASPM L1. Spec doesn't mention L0S.
	 */
	if (state & PCIE_LINK_STATE_L1)
		__pcie_aspm_config_one_dev(parent, state);

	list_for_each_entry(child, &linkbus->devices, bus_list)
		__pcie_aspm_config_one_dev(child, state);

	if (!(state & PCIE_LINK_STATE_L1))
		__pcie_aspm_config_one_dev(parent, state);

	link->aspm_enabled = state;
}

/* Check the whole hierarchy, and configure each link in the hierarchy */
static void __pcie_aspm_configure_link_state(struct pcie_link_state *link,
					     u32 state)
{
	struct pcie_link_state *leaf, *root = link->root;

	state &= (PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1);

	/* Check all links who have specific root port link */
	list_for_each_entry(leaf, &link_list, sibling) {
		if (!list_empty(&leaf->children) || (leaf->root != root))
			continue;
		state = pcie_aspm_check_state(leaf, state);
	}
	/* Check root port link too in case it hasn't children */
	state = pcie_aspm_check_state(root, state);
	if (link->aspm_enabled == state)
		return;
	/*
	 * We must change the hierarchy. See comments in
	 * __pcie_aspm_config_link for the order
	 **/
	if (state & PCIE_LINK_STATE_L1) {
		list_for_each_entry(leaf, &link_list, sibling) {
			if (leaf->root == root)
				__pcie_aspm_config_link(leaf, state);
		}
	} else {
		list_for_each_entry_reverse(leaf, &link_list, sibling) {
			if (leaf->root == root)
				__pcie_aspm_config_link(leaf, state);
		}
	}
}

/*
 * pcie_aspm_configure_link_state: enable/disable PCI express link state
 * @pdev: the root port or switch downstream port
 */
static void pcie_aspm_configure_link_state(struct pcie_link_state *link,
					   u32 state)
{
	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	__pcie_aspm_configure_link_state(link, state);
	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);
}

static void free_link_state(struct pcie_link_state *link)
{
	link->pdev->link_state = NULL;
	kfree(link);
}

static int pcie_aspm_sanity_check(struct pci_dev *pdev)
{
	struct pci_dev *child;
	int pos;
	u32 reg32;
	/*
	 * Some functions in a slot might not all be PCIE functions,
	 * very strange. Disable ASPM for the whole slot
	 */
	list_for_each_entry(child, &pdev->subordinate->devices, bus_list) {
		pos = pci_find_capability(child, PCI_CAP_ID_EXP);
		if (!pos)
			return -EINVAL;
		/*
		 * Disable ASPM for pre-1.1 PCIe device, we follow MS to use
		 * RBER bit to determine if a function is 1.1 version device
		 */
		pci_read_config_dword(child, pos + PCI_EXP_DEVCAP, &reg32);
		if (!(reg32 & PCI_EXP_DEVCAP_RBER) && !aspm_force) {
			dev_printk(KERN_INFO, &child->dev, "disabling ASPM"
				" on pre-1.1 PCIe device.  You can enable it"
				" with 'pcie_aspm=force'\n");
			return -EINVAL;
		}
	}
	return 0;
}

static struct pcie_link_state *pcie_aspm_setup_link_state(struct pci_dev *pdev)
{
	struct pcie_link_state *link;
	int blacklist = !!pcie_aspm_sanity_check(pdev);

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return NULL;
	INIT_LIST_HEAD(&link->sibling);
	INIT_LIST_HEAD(&link->children);
	INIT_LIST_HEAD(&link->link);
	link->pdev = pdev;
	if (pdev->pcie_type == PCI_EXP_TYPE_DOWNSTREAM) {
		struct pcie_link_state *parent;
		parent = pdev->bus->parent->self->link_state;
		if (!parent) {
			kfree(link);
			return NULL;
		}
		link->parent = parent;
		list_add(&link->link, &parent->children);
	}
	/* Setup a pointer to the root port link */
	if (!link->parent)
		link->root = link;
	else
		link->root = link->parent->root;

	list_add(&link->sibling, &link_list);

	pdev->link_state = link;

	/* Check ASPM capability */
	pcie_aspm_cap_init(link, blacklist);

	/* Check Clock PM capability */
	pcie_clkpm_cap_init(link, blacklist);

	return link;
}

/*
 * pcie_aspm_init_link_state: Initiate PCI express link state.
 * It is called after the pcie and its children devices are scaned.
 * @pdev: the root port or switch downstream port
 */
void pcie_aspm_init_link_state(struct pci_dev *pdev)
{
	u32 state;
	struct pcie_link_state *link;

	if (aspm_disabled || !pdev->is_pcie || pdev->link_state)
		return;
	if (pdev->pcie_type != PCI_EXP_TYPE_ROOT_PORT &&
	    pdev->pcie_type != PCI_EXP_TYPE_DOWNSTREAM)
		return;

	/* VIA has a strange chipset, root port is under a bridge */
	if (pdev->pcie_type == PCI_EXP_TYPE_ROOT_PORT &&
	    pdev->bus->self)
		return;

	down_read(&pci_bus_sem);
	if (list_empty(&pdev->subordinate->devices))
		goto out;

	mutex_lock(&aspm_lock);
	link = pcie_aspm_setup_link_state(pdev);
	if (!link)
		goto unlock;
	/*
	 * Setup initial ASPM state
	 *
	 * If link has switch, delay the link config. The leaf link
	 * initialization will config the whole hierarchy. But we must
	 * make sure BIOS doesn't set unsupported link state.
	 */
	if (pcie_aspm_downstream_has_switch(link)) {
		state = pcie_aspm_check_state(link, link->aspm_default);
		__pcie_aspm_config_link(link, state);
	} else {
		state = policy_to_aspm_state(link);
		__pcie_aspm_configure_link_state(link, state);
	}

	/* Setup initial Clock PM state */
	state = (link->clkpm_capable) ? policy_to_clkpm_state(link) : 0;
	pcie_set_clkpm(link, state);
unlock:
	mutex_unlock(&aspm_lock);
out:
	up_read(&pci_bus_sem);
}

/* @pdev: the endpoint device */
void pcie_aspm_exit_link_state(struct pci_dev *pdev)
{
	struct pci_dev *parent = pdev->bus->self;
	struct pcie_link_state *link_state = parent->link_state;

	if (aspm_disabled || !pdev->is_pcie || !parent || !link_state)
		return;
	if (parent->pcie_type != PCI_EXP_TYPE_ROOT_PORT &&
		parent->pcie_type != PCI_EXP_TYPE_DOWNSTREAM)
		return;
	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);

	/*
	 * All PCIe functions are in one slot, remove one function will remove
	 * the whole slot, so just wait until we are the last function left.
	 */
	if (!list_is_last(&pdev->bus_list, &parent->subordinate->devices))
		goto out;

	/* All functions are removed, so just disable ASPM for the link */
	__pcie_aspm_config_one_dev(parent, 0);
	list_del(&link_state->sibling);
	list_del(&link_state->link);
	/* Clock PM is for endpoint device */

	free_link_state(link_state);
out:
	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);
}

/* @pdev: the root port or switch downstream port */
void pcie_aspm_pm_state_change(struct pci_dev *pdev)
{
	struct pcie_link_state *link_state = pdev->link_state;

	if (aspm_disabled || !pdev->is_pcie || !pdev->link_state)
		return;
	if (pdev->pcie_type != PCI_EXP_TYPE_ROOT_PORT &&
		pdev->pcie_type != PCI_EXP_TYPE_DOWNSTREAM)
		return;
	/*
	 * devices changed PM state, we should recheck if latency meets all
	 * functions' requirement
	 */
	pcie_aspm_configure_link_state(link_state, link_state->aspm_enabled);
}

/*
 * pci_disable_link_state - disable pci device's link state, so the link will
 * never enter specific states
 */
void pci_disable_link_state(struct pci_dev *pdev, int state)
{
	struct pci_dev *parent = pdev->bus->self;
	struct pcie_link_state *link_state;

	if (aspm_disabled || !pdev->is_pcie)
		return;
	if (pdev->pcie_type == PCI_EXP_TYPE_ROOT_PORT ||
	    pdev->pcie_type == PCI_EXP_TYPE_DOWNSTREAM)
		parent = pdev;
	if (!parent || !parent->link_state)
		return;

	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	link_state = parent->link_state;
	link_state->aspm_support &= ~state;
	__pcie_aspm_configure_link_state(link_state, link_state->aspm_enabled);
	if (state & PCIE_LINK_STATE_CLKPM) {
		link_state->clkpm_capable = 0;
		pcie_set_clkpm(link_state, 0);
	}
	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);
}
EXPORT_SYMBOL(pci_disable_link_state);

static int pcie_aspm_set_policy(const char *val, struct kernel_param *kp)
{
	int i;
	struct pcie_link_state *link_state;

	for (i = 0; i < ARRAY_SIZE(policy_str); i++)
		if (!strncmp(val, policy_str[i], strlen(policy_str[i])))
			break;
	if (i >= ARRAY_SIZE(policy_str))
		return -EINVAL;
	if (i == aspm_policy)
		return 0;

	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	aspm_policy = i;
	list_for_each_entry(link_state, &link_list, sibling) {
		__pcie_aspm_configure_link_state(link_state,
			policy_to_aspm_state(link_state));
		pcie_set_clkpm(link_state, policy_to_clkpm_state(link_state));
	}
	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);
	return 0;
}

static int pcie_aspm_get_policy(char *buffer, struct kernel_param *kp)
{
	int i, cnt = 0;
	for (i = 0; i < ARRAY_SIZE(policy_str); i++)
		if (i == aspm_policy)
			cnt += sprintf(buffer + cnt, "[%s] ", policy_str[i]);
		else
			cnt += sprintf(buffer + cnt, "%s ", policy_str[i]);
	return cnt;
}

module_param_call(policy, pcie_aspm_set_policy, pcie_aspm_get_policy,
	NULL, 0644);

#ifdef CONFIG_PCIEASPM_DEBUG
static ssize_t link_state_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct pci_dev *pci_device = to_pci_dev(dev);
	struct pcie_link_state *link_state = pci_device->link_state;

	return sprintf(buf, "%d\n", link_state->aspm_enabled);
}

static ssize_t link_state_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t n)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int state;

	if (n < 1)
		return -EINVAL;
	state = buf[0]-'0';
	if (state >= 0 && state <= 3) {
		/* setup link aspm state */
		pcie_aspm_configure_link_state(pdev->link_state, state);
		return n;
	}

	return -EINVAL;
}

static ssize_t clk_ctl_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct pci_dev *pci_device = to_pci_dev(dev);
	struct pcie_link_state *link_state = pci_device->link_state;

	return sprintf(buf, "%d\n", link_state->clkpm_enabled);
}

static ssize_t clk_ctl_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t n)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int state;

	if (n < 1)
		return -EINVAL;
	state = buf[0]-'0';

	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	pcie_set_clkpm_nocheck(pdev->link_state, !!state);
	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);

	return n;
}

static DEVICE_ATTR(link_state, 0644, link_state_show, link_state_store);
static DEVICE_ATTR(clk_ctl, 0644, clk_ctl_show, clk_ctl_store);

static char power_group[] = "power";
void pcie_aspm_create_sysfs_dev_files(struct pci_dev *pdev)
{
	struct pcie_link_state *link_state = pdev->link_state;

	if (!pdev->is_pcie || (pdev->pcie_type != PCI_EXP_TYPE_ROOT_PORT &&
		pdev->pcie_type != PCI_EXP_TYPE_DOWNSTREAM) || !link_state)
		return;

	if (link_state->aspm_support)
		sysfs_add_file_to_group(&pdev->dev.kobj,
			&dev_attr_link_state.attr, power_group);
	if (link_state->clkpm_capable)
		sysfs_add_file_to_group(&pdev->dev.kobj,
			&dev_attr_clk_ctl.attr, power_group);
}

void pcie_aspm_remove_sysfs_dev_files(struct pci_dev *pdev)
{
	struct pcie_link_state *link_state = pdev->link_state;

	if (!pdev->is_pcie || (pdev->pcie_type != PCI_EXP_TYPE_ROOT_PORT &&
		pdev->pcie_type != PCI_EXP_TYPE_DOWNSTREAM) || !link_state)
		return;

	if (link_state->aspm_support)
		sysfs_remove_file_from_group(&pdev->dev.kobj,
			&dev_attr_link_state.attr, power_group);
	if (link_state->clkpm_capable)
		sysfs_remove_file_from_group(&pdev->dev.kobj,
			&dev_attr_clk_ctl.attr, power_group);
}
#endif

static int __init pcie_aspm_disable(char *str)
{
	if (!strcmp(str, "off")) {
		aspm_disabled = 1;
		printk(KERN_INFO "PCIe ASPM is disabled\n");
	} else if (!strcmp(str, "force")) {
		aspm_force = 1;
		printk(KERN_INFO "PCIe ASPM is forcedly enabled\n");
	}
	return 1;
}

__setup("pcie_aspm=", pcie_aspm_disable);

void pcie_no_aspm(void)
{
	if (!aspm_force)
		aspm_disabled = 1;
}

/**
 * pcie_aspm_enabled - is PCIe ASPM enabled?
 *
 * Returns true if ASPM has not been disabled by the command-line option
 * pcie_aspm=off.
 **/
int pcie_aspm_enabled(void)
{
       return !aspm_disabled;
}
EXPORT_SYMBOL(pcie_aspm_enabled);

