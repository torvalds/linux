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
#include <linux/aspm.h>
#include <acpi/acpi_bus.h>
#include <linux/pci-acpi.h>
#include "../pci.h"

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "pcie_aspm."

struct endpoint_state {
	unsigned int l0s_acceptable_latency;
	unsigned int l1_acceptable_latency;
};

struct pcie_link_state {
	struct list_head sibiling;
	struct pci_dev *pdev;

	/* ASPM state */
	unsigned int support_state;
	unsigned int enabled_state;
	unsigned int bios_aspm_state;
	/* upstream component */
	unsigned int l0s_upper_latency;
	unsigned int l1_upper_latency;
	/* downstream component */
	unsigned int l0s_down_latency;
	unsigned int l1_down_latency;
	/* Clock PM state*/
	unsigned int clk_pm_capable;
	unsigned int clk_pm_enabled;
	unsigned int bios_clk_state;

	/*
	 * A pcie downstream port only has one slot under it, so at most there
	 * are 8 functions
	 */
	struct endpoint_state endpoints[8];
};

static int aspm_disabled;
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

static int policy_to_aspm_state(struct pci_dev *pdev)
{
	struct pcie_link_state *link_state = pdev->link_state;

	switch (aspm_policy) {
	case POLICY_PERFORMANCE:
		/* Disable ASPM and Clock PM */
		return 0;
	case POLICY_POWERSAVE:
		/* Enable ASPM L0s/L1 */
		return PCIE_LINK_STATE_L0S|PCIE_LINK_STATE_L1;
	case POLICY_DEFAULT:
		return link_state->bios_aspm_state;
	}
	return 0;
}

static int policy_to_clkpm_state(struct pci_dev *pdev)
{
	struct pcie_link_state *link_state = pdev->link_state;

	switch (aspm_policy) {
	case POLICY_PERFORMANCE:
		/* Disable ASPM and Clock PM */
		return 0;
	case POLICY_POWERSAVE:
		/* Disable Clock PM */
		return 1;
	case POLICY_DEFAULT:
		return link_state->bios_clk_state;
	}
	return 0;
}

static void pcie_set_clock_pm(struct pci_dev *pdev, int enable)
{
	struct pci_dev *child_dev;
	int pos;
	u16 reg16;
	struct pcie_link_state *link_state = pdev->link_state;

	list_for_each_entry(child_dev, &pdev->subordinate->devices, bus_list) {
		pos = pci_find_capability(child_dev, PCI_CAP_ID_EXP);
		if (!pos)
			return;
		pci_read_config_word(child_dev, pos + PCI_EXP_LNKCTL, &reg16);
		if (enable)
			reg16 |= PCI_EXP_LNKCTL_CLKREQ_EN;
		else
			reg16 &= ~PCI_EXP_LNKCTL_CLKREQ_EN;
		pci_write_config_word(child_dev, pos + PCI_EXP_LNKCTL, reg16);
	}
	link_state->clk_pm_enabled = !!enable;
}

static void pcie_check_clock_pm(struct pci_dev *pdev)
{
	int pos;
	u32 reg32;
	u16 reg16;
	int capable = 1, enabled = 1;
	struct pci_dev *child_dev;
	struct pcie_link_state *link_state = pdev->link_state;

	/* All functions should have the same cap and state, take the worst */
	list_for_each_entry(child_dev, &pdev->subordinate->devices, bus_list) {
		pos = pci_find_capability(child_dev, PCI_CAP_ID_EXP);
		if (!pos)
			return;
		pci_read_config_dword(child_dev, pos + PCI_EXP_LNKCAP, &reg32);
		if (!(reg32 & PCI_EXP_LNKCAP_CLKPM)) {
			capable = 0;
			enabled = 0;
			break;
		}
		pci_read_config_word(child_dev, pos + PCI_EXP_LNKCTL, &reg16);
		if (!(reg16 & PCI_EXP_LNKCTL_CLKREQ_EN))
			enabled = 0;
	}
	link_state->clk_pm_capable = capable;
	link_state->clk_pm_enabled = enabled;
	link_state->bios_clk_state = enabled;
	pcie_set_clock_pm(pdev, policy_to_clkpm_state(pdev));
}

/*
 * pcie_aspm_configure_common_clock: check if the 2 ends of a link
 *   could use common clock. If they are, configure them to use the
 *   common clock. That will reduce the ASPM state exit latency.
 */
static void pcie_aspm_configure_common_clock(struct pci_dev *pdev)
{
	int pos, child_pos;
	u16 reg16 = 0;
	struct pci_dev *child_dev;
	int same_clock = 1;

	/*
	 * all functions of a slot should have the same Slot Clock
	 * Configuration, so just check one function
	 * */
	child_dev = list_entry(pdev->subordinate->devices.next, struct pci_dev,
		bus_list);
	BUG_ON(!child_dev->is_pcie);

	/* Check downstream component if bit Slot Clock Configuration is 1 */
	child_pos = pci_find_capability(child_dev, PCI_CAP_ID_EXP);
	pci_read_config_word(child_dev, child_pos + PCI_EXP_LNKSTA, &reg16);
	if (!(reg16 & PCI_EXP_LNKSTA_SLC))
		same_clock = 0;

	/* Check upstream component if bit Slot Clock Configuration is 1 */
	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	pci_read_config_word(pdev, pos + PCI_EXP_LNKSTA, &reg16);
	if (!(reg16 & PCI_EXP_LNKSTA_SLC))
		same_clock = 0;

	/* Configure downstream component, all functions */
	list_for_each_entry(child_dev, &pdev->subordinate->devices, bus_list) {
		child_pos = pci_find_capability(child_dev, PCI_CAP_ID_EXP);
		pci_read_config_word(child_dev, child_pos + PCI_EXP_LNKCTL,
			&reg16);
		if (same_clock)
			reg16 |= PCI_EXP_LNKCTL_CCC;
		else
			reg16 &= ~PCI_EXP_LNKCTL_CCC;
		pci_write_config_word(child_dev, child_pos + PCI_EXP_LNKCTL,
			reg16);
	}

	/* Configure upstream component */
	pci_read_config_word(pdev, pos + PCI_EXP_LNKCTL, &reg16);
	if (same_clock)
		reg16 |= PCI_EXP_LNKCTL_CCC;
	else
		reg16 &= ~PCI_EXP_LNKCTL_CCC;
	pci_write_config_word(pdev, pos + PCI_EXP_LNKCTL, reg16);

	/* retrain link */
	reg16 |= PCI_EXP_LNKCTL_RL;
	pci_write_config_word(pdev, pos + PCI_EXP_LNKCTL, reg16);

	/* Wait for link training end */
	while (1) {
		pci_read_config_word(pdev, pos + PCI_EXP_LNKSTA, &reg16);
		if (!(reg16 & PCI_EXP_LNKSTA_LT))
			break;
		cpu_relax();
	}
}

/*
 * calc_L0S_latency: Convert L0s latency encoding to ns
 */
static unsigned int calc_L0S_latency(unsigned int latency_encoding, int ac)
{
	unsigned int ns = 64;

	if (latency_encoding == 0x7) {
		if (ac)
			ns = -1U;
		else
			ns = 5*1000; /* > 4us */
	} else
		ns *= (1 << latency_encoding);
	return ns;
}

/*
 * calc_L1_latency: Convert L1 latency encoding to ns
 */
static unsigned int calc_L1_latency(unsigned int latency_encoding, int ac)
{
	unsigned int ns = 1000;

	if (latency_encoding == 0x7) {
		if (ac)
			ns = -1U;
		else
			ns = 65*1000; /* > 64us */
	} else
		ns *= (1 << latency_encoding);
	return ns;
}

static void pcie_aspm_get_cap_device(struct pci_dev *pdev, u32 *state,
	unsigned int *l0s, unsigned int *l1, unsigned int *enabled)
{
	int pos;
	u16 reg16;
	u32 reg32;
	unsigned int latency;

	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	pci_read_config_dword(pdev, pos + PCI_EXP_LNKCAP, &reg32);
	*state = (reg32 & PCI_EXP_LNKCAP_ASPMS) >> 10;
	if (*state != PCIE_LINK_STATE_L0S &&
		*state != (PCIE_LINK_STATE_L1|PCIE_LINK_STATE_L0S))
		* state = 0;
	if (*state == 0)
		return;

	latency = (reg32 & PCI_EXP_LNKCAP_L0SEL) >> 12;
	*l0s = calc_L0S_latency(latency, 0);
	if (*state & PCIE_LINK_STATE_L1) {
		latency = (reg32 & PCI_EXP_LNKCAP_L1EL) >> 15;
		*l1 = calc_L1_latency(latency, 0);
	}
	pci_read_config_word(pdev, pos + PCI_EXP_LNKCTL, &reg16);
	*enabled = reg16 & (PCIE_LINK_STATE_L0S|PCIE_LINK_STATE_L1);
}

static void pcie_aspm_cap_init(struct pci_dev *pdev)
{
	struct pci_dev *child_dev;
	u32 state, tmp;
	struct pcie_link_state *link_state = pdev->link_state;

	/* upstream component states */
	pcie_aspm_get_cap_device(pdev, &link_state->support_state,
		&link_state->l0s_upper_latency,
		&link_state->l1_upper_latency,
		&link_state->enabled_state);
	/* downstream component states, all functions have the same setting */
	child_dev = list_entry(pdev->subordinate->devices.next, struct pci_dev,
		bus_list);
	pcie_aspm_get_cap_device(child_dev, &state,
		&link_state->l0s_down_latency,
		&link_state->l1_down_latency,
		&tmp);
	link_state->support_state &= state;
	if (!link_state->support_state)
		return;
	link_state->enabled_state &= link_state->support_state;
	link_state->bios_aspm_state = link_state->enabled_state;

	/* ENDPOINT states*/
	list_for_each_entry(child_dev, &pdev->subordinate->devices, bus_list) {
		int pos;
		u32 reg32;
		unsigned int latency;
		struct endpoint_state *ep_state =
			&link_state->endpoints[PCI_FUNC(child_dev->devfn)];

		if (child_dev->pcie_type != PCI_EXP_TYPE_ENDPOINT &&
			child_dev->pcie_type != PCI_EXP_TYPE_LEG_END)
			continue;

		pos = pci_find_capability(child_dev, PCI_CAP_ID_EXP);
		pci_read_config_dword(child_dev, pos + PCI_EXP_DEVCAP, &reg32);
		latency = (reg32 & PCI_EXP_DEVCAP_L0S) >> 6;
		latency = calc_L0S_latency(latency, 1);
		ep_state->l0s_acceptable_latency = latency;
		if (link_state->support_state & PCIE_LINK_STATE_L1) {
			latency = (reg32 & PCI_EXP_DEVCAP_L1) >> 9;
			latency = calc_L1_latency(latency, 1);
			ep_state->l1_acceptable_latency = latency;
		}
	}
}

static unsigned int __pcie_aspm_check_state_one(struct pci_dev *pdev,
	unsigned int state)
{
	struct pci_dev *parent_dev, *tmp_dev;
	unsigned int latency, l1_latency = 0;
	struct pcie_link_state *link_state;
	struct endpoint_state *ep_state;

	parent_dev = pdev->bus->self;
	link_state = parent_dev->link_state;
	state &= link_state->support_state;
	if (state == 0)
		return 0;
	ep_state = &link_state->endpoints[PCI_FUNC(pdev->devfn)];

	/*
	 * Check latency for endpoint device.
	 * TBD: The latency from the endpoint to root complex vary per
	 * switch's upstream link state above the device. Here we just do a
	 * simple check which assumes all links above the device can be in L1
	 * state, that is we just consider the worst case. If switch's upstream
	 * link can't be put into L0S/L1, then our check is too strictly.
	 */
	tmp_dev = pdev;
	while (state & (PCIE_LINK_STATE_L0S | PCIE_LINK_STATE_L1)) {
		parent_dev = tmp_dev->bus->self;
		link_state = parent_dev->link_state;
		if (state & PCIE_LINK_STATE_L0S) {
			latency = max_t(unsigned int,
					link_state->l0s_upper_latency,
					link_state->l0s_down_latency);
			if (latency > ep_state->l0s_acceptable_latency)
				state &= ~PCIE_LINK_STATE_L0S;
		}
		if (state & PCIE_LINK_STATE_L1) {
			latency = max_t(unsigned int,
					link_state->l1_upper_latency,
					link_state->l1_down_latency);
			if (latency + l1_latency >
					ep_state->l1_acceptable_latency)
				state &= ~PCIE_LINK_STATE_L1;
		}
		if (!parent_dev->bus->self) /* parent_dev is a root port */
			break;
		else {
			/*
			 * parent_dev is the downstream port of a switch, make
			 * tmp_dev the upstream port of the switch
			 */
			tmp_dev = parent_dev->bus->self;
			/*
			 * every switch on the path to root complex need 1 more
			 * microsecond for L1. Spec doesn't mention L0S.
			 */
			if (state & PCIE_LINK_STATE_L1)
				l1_latency += 1000;
		}
	}
	return state;
}

static unsigned int pcie_aspm_check_state(struct pci_dev *pdev,
	unsigned int state)
{
	struct pci_dev *child_dev;

	/* If no child, disable the link */
	if (list_empty(&pdev->subordinate->devices))
		return 0;
	list_for_each_entry(child_dev, &pdev->subordinate->devices, bus_list) {
		if (child_dev->pcie_type == PCI_EXP_TYPE_PCI_BRIDGE) {
			/*
			 * If downstream component of a link is pci bridge, we
			 * disable ASPM for now for the link
			 * */
			state = 0;
			break;
		}
		if ((child_dev->pcie_type != PCI_EXP_TYPE_ENDPOINT &&
			child_dev->pcie_type != PCI_EXP_TYPE_LEG_END))
			continue;
		/* Device not in D0 doesn't need check latency */
		if (child_dev->current_state == PCI_D1 ||
			child_dev->current_state == PCI_D2 ||
			child_dev->current_state == PCI_D3hot ||
			child_dev->current_state == PCI_D3cold)
			continue;
		state = __pcie_aspm_check_state_one(child_dev, state);
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

static void __pcie_aspm_config_link(struct pci_dev *pdev, unsigned int state)
{
	struct pci_dev *child_dev;
	int valid = 1;
	struct pcie_link_state *link_state = pdev->link_state;

	/*
	 * if the downstream component has pci bridge function, don't do ASPM
	 * now
	 */
	list_for_each_entry(child_dev, &pdev->subordinate->devices, bus_list) {
		if (child_dev->pcie_type == PCI_EXP_TYPE_PCI_BRIDGE) {
			valid = 0;
			break;
		}
	}
	if (!valid)
		return;

	/*
	 * spec 2.0 suggests all functions should be configured the same
	 * setting for ASPM. Enabling ASPM L1 should be done in upstream
	 * component first and then downstream, and vice versa for disabling
	 * ASPM L1. Spec doesn't mention L0S.
	 */
	if (state & PCIE_LINK_STATE_L1)
		__pcie_aspm_config_one_dev(pdev, state);

	list_for_each_entry(child_dev, &pdev->subordinate->devices, bus_list)
		__pcie_aspm_config_one_dev(child_dev, state);

	if (!(state & PCIE_LINK_STATE_L1))
		__pcie_aspm_config_one_dev(pdev, state);

	link_state->enabled_state = state;
}

static void __pcie_aspm_configure_link_state(struct pci_dev *pdev,
	unsigned int state)
{
	struct pcie_link_state *link_state = pdev->link_state;

	if (link_state->support_state == 0)
		return;
	state &= PCIE_LINK_STATE_L0S|PCIE_LINK_STATE_L1;

	/* state 0 means disabling aspm */
	state = pcie_aspm_check_state(pdev, state);
	if (link_state->enabled_state == state)
		return;
	__pcie_aspm_config_link(pdev, state);
}

/*
 * pcie_aspm_configure_link_state: enable/disable PCI express link state
 * @pdev: the root port or switch downstream port
 */
static void pcie_aspm_configure_link_state(struct pci_dev *pdev,
	unsigned int state)
{
	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	__pcie_aspm_configure_link_state(pdev, state);
	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);
}

static void free_link_state(struct pci_dev *pdev)
{
	kfree(pdev->link_state);
	pdev->link_state = NULL;
}

/*
 * pcie_aspm_init_link_state: Initiate PCI express link state.
 * It is called after the pcie and its children devices are scaned.
 * @pdev: the root port or switch downstream port
 */
void pcie_aspm_init_link_state(struct pci_dev *pdev)
{
	unsigned int state;
	struct pcie_link_state *link_state;
	int error = 0;

	if (aspm_disabled || !pdev->is_pcie || pdev->link_state)
		return;
	if (pdev->pcie_type != PCI_EXP_TYPE_ROOT_PORT &&
		pdev->pcie_type != PCI_EXP_TYPE_DOWNSTREAM)
		return;
	down_read(&pci_bus_sem);
	if (list_empty(&pdev->subordinate->devices))
		goto out;

	mutex_lock(&aspm_lock);

	link_state = kzalloc(sizeof(*link_state), GFP_KERNEL);
	if (!link_state)
		goto unlock_out;
	pdev->link_state = link_state;

	pcie_aspm_configure_common_clock(pdev);

	pcie_aspm_cap_init(pdev);

	/* config link state to avoid BIOS error */
	state = pcie_aspm_check_state(pdev, policy_to_aspm_state(pdev));
	__pcie_aspm_config_link(pdev, state);

	pcie_check_clock_pm(pdev);

	link_state->pdev = pdev;
	list_add(&link_state->sibiling, &link_list);

unlock_out:
	if (error)
		free_link_state(pdev);
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
	 * the the whole slot, so just wait
	 */
	if (!list_empty(&parent->subordinate->devices))
		goto out;

	/* All functions are removed, so just disable ASPM for the link */
	__pcie_aspm_config_one_dev(parent, 0);
	list_del(&link_state->sibiling);
	/* Clock PM is for endpoint device */

	free_link_state(parent);
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
	pcie_aspm_configure_link_state(pdev, link_state->enabled_state);
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
	if (!parent)
		return;

	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	link_state = parent->link_state;
	link_state->support_state &=
		~(state & (PCIE_LINK_STATE_L0S|PCIE_LINK_STATE_L1));
	if (state & PCIE_LINK_STATE_CLKPM)
		link_state->clk_pm_capable = 0;

	__pcie_aspm_configure_link_state(parent, link_state->enabled_state);
	if (!link_state->clk_pm_capable && link_state->clk_pm_enabled)
		pcie_set_clock_pm(parent, 0);
	mutex_unlock(&aspm_lock);
	up_read(&pci_bus_sem);
}
EXPORT_SYMBOL(pci_disable_link_state);

static int pcie_aspm_set_policy(const char *val, struct kernel_param *kp)
{
	int i;
	struct pci_dev *pdev;
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
	list_for_each_entry(link_state, &link_list, sibiling) {
		pdev = link_state->pdev;
		__pcie_aspm_configure_link_state(pdev,
			policy_to_aspm_state(pdev));
		if (link_state->clk_pm_capable &&
		    link_state->clk_pm_enabled != policy_to_clkpm_state(pdev))
			pcie_set_clock_pm(pdev, policy_to_clkpm_state(pdev));

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

	return sprintf(buf, "%d\n", link_state->enabled_state);
}

static ssize_t link_state_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t n)
{
	struct pci_dev *pci_device = to_pci_dev(dev);
	int state;

	if (n < 1)
		return -EINVAL;
	state = buf[0]-'0';
	if (state >= 0 && state <= 3) {
		/* setup link aspm state */
		pcie_aspm_configure_link_state(pci_device, state);
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

	return sprintf(buf, "%d\n", link_state->clk_pm_enabled);
}

static ssize_t clk_ctl_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t n)
{
	struct pci_dev *pci_device = to_pci_dev(dev);
	int state;

	if (n < 1)
		return -EINVAL;
	state = buf[0]-'0';

	down_read(&pci_bus_sem);
	mutex_lock(&aspm_lock);
	pcie_set_clock_pm(pci_device, !!state);
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
		pdev->pcie_type != PCI_EXP_TYPE_DOWNSTREAM))
		return;

	if (link_state->support_state)
		sysfs_add_file_to_group(&pdev->dev.kobj,
			&dev_attr_link_state.attr, power_group);
	if (link_state->clk_pm_capable)
		sysfs_add_file_to_group(&pdev->dev.kobj,
			&dev_attr_clk_ctl.attr, power_group);
}

void pcie_aspm_remove_sysfs_dev_files(struct pci_dev *pdev)
{
	struct pcie_link_state *link_state = pdev->link_state;

	if (!pdev->is_pcie || (pdev->pcie_type != PCI_EXP_TYPE_ROOT_PORT &&
		pdev->pcie_type != PCI_EXP_TYPE_DOWNSTREAM))
		return;

	if (link_state->support_state)
		sysfs_remove_file_from_group(&pdev->dev.kobj,
			&dev_attr_link_state.attr, power_group);
	if (link_state->clk_pm_capable)
		sysfs_remove_file_from_group(&pdev->dev.kobj,
			&dev_attr_clk_ctl.attr, power_group);
}
#endif

static int __init pcie_aspm_disable(char *str)
{
	aspm_disabled = 1;
	return 1;
}

__setup("pcie_noaspm", pcie_aspm_disable);

static int __init pcie_aspm_init(void)
{
	if (aspm_disabled)
		return 0;
	pci_osc_support_set(OSC_ACTIVE_STATE_PWR_SUPPORT|
		OSC_CLOCK_PWR_CAPABILITY_SUPPORT);
	return 0;
}

fs_initcall(pcie_aspm_init);
