/*
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>
#include <linux/soc/qcom/smem.h>
#include <linux/soc/qcom/smem_state.h>

/*
 * This driver implements the Qualcomm Shared Memory State Machine, a mechanism
 * for communicating single bit state information to remote processors.
 *
 * The implementation is based on two sections of shared memory; the first
 * holding the state bits and the second holding a matrix of subscription bits.
 *
 * The state bits are structured in entries of 32 bits, each belonging to one
 * system in the SoC. The entry belonging to the local system is considered
 * read-write, while the rest should be considered read-only.
 *
 * The subscription matrix consists of N bitmaps per entry, denoting interest
 * in updates of the entry for each of the N hosts. Upon updating a state bit
 * each host's subscription bitmap should be queried and the remote system
 * should be interrupted if they request so.
 *
 * The subscription matrix is laid out in entry-major order:
 * entry0: [host0 ... hostN]
 *	.
 *	.
 * entryM: [host0 ... hostN]
 *
 * A third, optional, shared memory region might contain information regarding
 * the number of entries in the state bitmap as well as number of columns in
 * the subscription matrix.
 */

/*
 * Shared memory identifiers, used to acquire handles to respective memory
 * region.
 */
#define SMEM_SMSM_SHARED_STATE		85
#define SMEM_SMSM_CPU_INTR_MASK		333
#define SMEM_SMSM_SIZE_INFO		419

/*
 * Default sizes, in case SMEM_SMSM_SIZE_INFO is not found.
 */
#define SMSM_DEFAULT_NUM_ENTRIES	8
#define SMSM_DEFAULT_NUM_HOSTS		3

struct smsm_entry;
struct smsm_host;

/**
 * struct qcom_smsm - smsm driver context
 * @dev:	smsm device pointer
 * @local_host:	column in the subscription matrix representing this system
 * @num_hosts:	number of columns in the subscription matrix
 * @num_entries: number of entries in the state map and rows in the subscription
 *		matrix
 * @local_state: pointer to the local processor's state bits
 * @subscription: pointer to local processor's row in subscription matrix
 * @state:	smem state handle
 * @lock:	spinlock for read-modify-write of the outgoing state
 * @entries:	context for each of the entries
 * @hosts:	context for each of the hosts
 */
struct qcom_smsm {
	struct device *dev;

	u32 local_host;

	u32 num_hosts;
	u32 num_entries;

	u32 *local_state;
	u32 *subscription;
	struct qcom_smem_state *state;

	spinlock_t lock;

	struct smsm_entry *entries;
	struct smsm_host *hosts;
};

/**
 * struct smsm_entry - per remote processor entry context
 * @smsm:	back-reference to driver context
 * @domain:	IRQ domain for this entry, if representing a remote system
 * @irq_enabled: bitmap of which state bits IRQs are enabled
 * @irq_rising:	bitmap tracking if rising bits should be propagated
 * @irq_falling: bitmap tracking if falling bits should be propagated
 * @last_value:	snapshot of state bits last time the interrupts where propagated
 * @remote_state: pointer to this entry's state bits
 * @subscription: pointer to a row in the subscription matrix representing this
 *		entry
 */
struct smsm_entry {
	struct qcom_smsm *smsm;

	struct irq_domain *domain;
	DECLARE_BITMAP(irq_enabled, 32);
	DECLARE_BITMAP(irq_rising, 32);
	DECLARE_BITMAP(irq_falling, 32);
	u32 last_value;

	u32 *remote_state;
	u32 *subscription;
};

/**
 * struct smsm_host - representation of a remote host
 * @ipc_regmap:	regmap for outgoing interrupt
 * @ipc_offset:	offset in @ipc_regmap for outgoing interrupt
 * @ipc_bit:	bit in @ipc_regmap + @ipc_offset for outgoing interrupt
 */
struct smsm_host {
	struct regmap *ipc_regmap;
	int ipc_offset;
	int ipc_bit;
};

/**
 * smsm_update_bits() - change bit in outgoing entry and inform subscribers
 * @data:	smsm context pointer
 * @offset:	bit in the entry
 * @value:	new value
 *
 * Used to set and clear the bits in the outgoing/local entry and inform
 * subscribers about the change.
 */
static int smsm_update_bits(void *data, u32 mask, u32 value)
{
	struct qcom_smsm *smsm = data;
	struct smsm_host *hostp;
	unsigned long flags;
	u32 changes;
	u32 host;
	u32 orig;
	u32 val;

	spin_lock_irqsave(&smsm->lock, flags);

	/* Update the entry */
	val = orig = readl(smsm->local_state);
	val &= ~mask;
	val |= value;

	/* Don't signal if we didn't change the value */
	changes = val ^ orig;
	if (!changes) {
		spin_unlock_irqrestore(&smsm->lock, flags);
		goto done;
	}

	/* Write out the new value */
	writel(val, smsm->local_state);
	spin_unlock_irqrestore(&smsm->lock, flags);

	/* Make sure the value update is ordered before any kicks */
	wmb();

	/* Iterate over all hosts to check whom wants a kick */
	for (host = 0; host < smsm->num_hosts; host++) {
		hostp = &smsm->hosts[host];

		val = readl(smsm->subscription + host);
		if (val & changes && hostp->ipc_regmap) {
			regmap_write(hostp->ipc_regmap,
				     hostp->ipc_offset,
				     BIT(hostp->ipc_bit));
		}
	}

done:
	return 0;
}

static const struct qcom_smem_state_ops smsm_state_ops = {
	.update_bits = smsm_update_bits,
};

/**
 * smsm_intr() - cascading IRQ handler for SMSM
 * @irq:	unused
 * @data:	entry related to this IRQ
 *
 * This function cascades an incoming interrupt from a remote system, based on
 * the state bits and configuration.
 */
static irqreturn_t smsm_intr(int irq, void *data)
{
	struct smsm_entry *entry = data;
	unsigned i;
	int irq_pin;
	u32 changed;
	u32 val;

	val = readl(entry->remote_state);
	changed = val ^ entry->last_value;
	entry->last_value = val;

	for_each_set_bit(i, entry->irq_enabled, 32) {
		if (!(changed & BIT(i)))
			continue;

		if (val & BIT(i)) {
			if (test_bit(i, entry->irq_rising)) {
				irq_pin = irq_find_mapping(entry->domain, i);
				handle_nested_irq(irq_pin);
			}
		} else {
			if (test_bit(i, entry->irq_falling)) {
				irq_pin = irq_find_mapping(entry->domain, i);
				handle_nested_irq(irq_pin);
			}
		}
	}

	return IRQ_HANDLED;
}

/**
 * smsm_mask_irq() - un-subscribe from cascades of IRQs of a certain staus bit
 * @irqd:	IRQ handle to be masked
 *
 * This un-subscribes the local CPU from interrupts upon changes to the defines
 * status bit. The bit is also cleared from cascading.
 */
static void smsm_mask_irq(struct irq_data *irqd)
{
	struct smsm_entry *entry = irq_data_get_irq_chip_data(irqd);
	irq_hw_number_t irq = irqd_to_hwirq(irqd);
	struct qcom_smsm *smsm = entry->smsm;
	u32 val;

	if (entry->subscription) {
		val = readl(entry->subscription + smsm->local_host);
		val &= ~BIT(irq);
		writel(val, entry->subscription + smsm->local_host);
	}

	clear_bit(irq, entry->irq_enabled);
}

/**
 * smsm_unmask_irq() - subscribe to cascades of IRQs of a certain status bit
 * @irqd:	IRQ handle to be unmasked
 *

 * This subscribes the local CPU to interrupts upon changes to the defined
 * status bit. The bit is also marked for cascading.

 */
static void smsm_unmask_irq(struct irq_data *irqd)
{
	struct smsm_entry *entry = irq_data_get_irq_chip_data(irqd);
	irq_hw_number_t irq = irqd_to_hwirq(irqd);
	struct qcom_smsm *smsm = entry->smsm;
	u32 val;

	set_bit(irq, entry->irq_enabled);

	if (entry->subscription) {
		val = readl(entry->subscription + smsm->local_host);
		val |= BIT(irq);
		writel(val, entry->subscription + smsm->local_host);
	}
}

/**
 * smsm_set_irq_type() - updates the requested IRQ type for the cascading
 * @irqd:	consumer interrupt handle
 * @type:	requested flags
 */
static int smsm_set_irq_type(struct irq_data *irqd, unsigned int type)
{
	struct smsm_entry *entry = irq_data_get_irq_chip_data(irqd);
	irq_hw_number_t irq = irqd_to_hwirq(irqd);

	if (!(type & IRQ_TYPE_EDGE_BOTH))
		return -EINVAL;

	if (type & IRQ_TYPE_EDGE_RISING)
		set_bit(irq, entry->irq_rising);
	else
		clear_bit(irq, entry->irq_rising);

	if (type & IRQ_TYPE_EDGE_FALLING)
		set_bit(irq, entry->irq_falling);
	else
		clear_bit(irq, entry->irq_falling);

	return 0;
}

static struct irq_chip smsm_irq_chip = {
	.name           = "smsm",
	.irq_mask       = smsm_mask_irq,
	.irq_unmask     = smsm_unmask_irq,
	.irq_set_type	= smsm_set_irq_type,
};

/**
 * smsm_irq_map() - sets up a mapping for a cascaded IRQ
 * @d:		IRQ domain representing an entry
 * @irq:	IRQ to set up
 * @hw:		unused
 */
static int smsm_irq_map(struct irq_domain *d,
			unsigned int irq,
			irq_hw_number_t hw)
{
	struct smsm_entry *entry = d->host_data;

	irq_set_chip_and_handler(irq, &smsm_irq_chip, handle_level_irq);
	irq_set_chip_data(irq, entry);
	irq_set_nested_thread(irq, 1);

	return 0;
}

static const struct irq_domain_ops smsm_irq_ops = {
	.map = smsm_irq_map,
	.xlate = irq_domain_xlate_twocell,
};

/**
 * smsm_parse_ipc() - parses a qcom,ipc-%d device tree property
 * @smsm:	smsm driver context
 * @host_id:	index of the remote host to be resolved
 *
 * Parses device tree to acquire the information needed for sending the
 * outgoing interrupts to a remote host - identified by @host_id.
 */
static int smsm_parse_ipc(struct qcom_smsm *smsm, unsigned host_id)
{
	struct device_node *syscon;
	struct device_node *node = smsm->dev->of_node;
	struct smsm_host *host = &smsm->hosts[host_id];
	char key[16];
	int ret;

	snprintf(key, sizeof(key), "qcom,ipc-%d", host_id);
	syscon = of_parse_phandle(node, key, 0);
	if (!syscon)
		return 0;

	host->ipc_regmap = syscon_node_to_regmap(syscon);
	if (IS_ERR(host->ipc_regmap))
		return PTR_ERR(host->ipc_regmap);

	ret = of_property_read_u32_index(node, key, 1, &host->ipc_offset);
	if (ret < 0) {
		dev_err(smsm->dev, "no offset in %s\n", key);
		return -EINVAL;
	}

	ret = of_property_read_u32_index(node, key, 2, &host->ipc_bit);
	if (ret < 0) {
		dev_err(smsm->dev, "no bit in %s\n", key);
		return -EINVAL;
	}

	return 0;
}

/**
 * smsm_inbound_entry() - parse DT and set up an entry representing a remote system
 * @smsm:	smsm driver context
 * @entry:	entry context to be set up
 * @node:	dt node containing the entry's properties
 */
static int smsm_inbound_entry(struct qcom_smsm *smsm,
			      struct smsm_entry *entry,
			      struct device_node *node)
{
	int ret;
	int irq;

	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		dev_err(smsm->dev, "failed to parse smsm interrupt\n");
		return -EINVAL;
	}

	ret = devm_request_threaded_irq(smsm->dev, irq,
					NULL, smsm_intr,
					IRQF_ONESHOT,
					"smsm", (void *)entry);
	if (ret) {
		dev_err(smsm->dev, "failed to request interrupt\n");
		return ret;
	}

	entry->domain = irq_domain_add_linear(node, 32, &smsm_irq_ops, entry);
	if (!entry->domain) {
		dev_err(smsm->dev, "failed to add irq_domain\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * smsm_get_size_info() - parse the optional memory segment for sizes
 * @smsm:	smsm driver context
 *
 * Attempt to acquire the number of hosts and entries from the optional shared
 * memory location. Not being able to find this segment should indicate that
 * we're on a older system where these values was hard coded to
 * SMSM_DEFAULT_NUM_ENTRIES and SMSM_DEFAULT_NUM_HOSTS.
 *
 * Returns 0 on success, negative errno on failure.
 */
static int smsm_get_size_info(struct qcom_smsm *smsm)
{
	size_t size;
	struct {
		u32 num_hosts;
		u32 num_entries;
		u32 reserved0;
		u32 reserved1;
	} *info;

	info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_SMSM_SIZE_INFO, &size);
	if (PTR_ERR(info) == -ENOENT || size != sizeof(*info)) {
		dev_warn(smsm->dev, "no smsm size info, using defaults\n");
		smsm->num_entries = SMSM_DEFAULT_NUM_ENTRIES;
		smsm->num_hosts = SMSM_DEFAULT_NUM_HOSTS;
		return 0;
	} else if (IS_ERR(info)) {
		dev_err(smsm->dev, "unable to retrieve smsm size info\n");
		return PTR_ERR(info);
	}

	smsm->num_entries = info->num_entries;
	smsm->num_hosts = info->num_hosts;

	dev_dbg(smsm->dev,
		"found custom size of smsm: %d entries %d hosts\n",
		smsm->num_entries, smsm->num_hosts);

	return 0;
}

static int qcom_smsm_probe(struct platform_device *pdev)
{
	struct device_node *local_node;
	struct device_node *node;
	struct smsm_entry *entry;
	struct qcom_smsm *smsm;
	u32 *intr_mask;
	size_t size;
	u32 *states;
	u32 id;
	int ret;

	smsm = devm_kzalloc(&pdev->dev, sizeof(*smsm), GFP_KERNEL);
	if (!smsm)
		return -ENOMEM;
	smsm->dev = &pdev->dev;
	spin_lock_init(&smsm->lock);

	ret = smsm_get_size_info(smsm);
	if (ret)
		return ret;

	smsm->entries = devm_kcalloc(&pdev->dev,
				     smsm->num_entries,
				     sizeof(struct smsm_entry),
				     GFP_KERNEL);
	if (!smsm->entries)
		return -ENOMEM;

	smsm->hosts = devm_kcalloc(&pdev->dev,
				   smsm->num_hosts,
				   sizeof(struct smsm_host),
				   GFP_KERNEL);
	if (!smsm->hosts)
		return -ENOMEM;

	local_node = of_find_node_with_property(pdev->dev.of_node, "#qcom,smem-state-cells");
	if (!local_node) {
		dev_err(&pdev->dev, "no state entry\n");
		return -EINVAL;
	}

	of_property_read_u32(pdev->dev.of_node,
			     "qcom,local-host",
			     &smsm->local_host);

	/* Parse the host properties */
	for (id = 0; id < smsm->num_hosts; id++) {
		ret = smsm_parse_ipc(smsm, id);
		if (ret < 0)
			return ret;
	}

	/* Acquire the main SMSM state vector */
	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_SMSM_SHARED_STATE,
			      smsm->num_entries * sizeof(u32));
	if (ret < 0 && ret != -EEXIST) {
		dev_err(&pdev->dev, "unable to allocate shared state entry\n");
		return ret;
	}

	states = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_SMSM_SHARED_STATE, NULL);
	if (IS_ERR(states)) {
		dev_err(&pdev->dev, "Unable to acquire shared state entry\n");
		return PTR_ERR(states);
	}

	/* Acquire the list of interrupt mask vectors */
	size = smsm->num_entries * smsm->num_hosts * sizeof(u32);
	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_SMSM_CPU_INTR_MASK, size);
	if (ret < 0 && ret != -EEXIST) {
		dev_err(&pdev->dev, "unable to allocate smsm interrupt mask\n");
		return ret;
	}

	intr_mask = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_SMSM_CPU_INTR_MASK, NULL);
	if (IS_ERR(intr_mask)) {
		dev_err(&pdev->dev, "unable to acquire shared memory interrupt mask\n");
		return PTR_ERR(intr_mask);
	}

	/* Setup the reference to the local state bits */
	smsm->local_state = states + smsm->local_host;
	smsm->subscription = intr_mask + smsm->local_host * smsm->num_hosts;

	/* Register the outgoing state */
	smsm->state = qcom_smem_state_register(local_node, &smsm_state_ops, smsm);
	if (IS_ERR(smsm->state)) {
		dev_err(smsm->dev, "failed to register qcom_smem_state\n");
		return PTR_ERR(smsm->state);
	}

	/* Register handlers for remote processor entries of interest. */
	for_each_available_child_of_node(pdev->dev.of_node, node) {
		if (!of_property_read_bool(node, "interrupt-controller"))
			continue;

		ret = of_property_read_u32(node, "reg", &id);
		if (ret || id >= smsm->num_entries) {
			dev_err(&pdev->dev, "invalid reg of entry\n");
			if (!ret)
				ret = -EINVAL;
			goto unwind_interfaces;
		}
		entry = &smsm->entries[id];

		entry->smsm = smsm;
		entry->remote_state = states + id;

		/* Setup subscription pointers and unsubscribe to any kicks */
		entry->subscription = intr_mask + id * smsm->num_hosts;
		writel(0, entry->subscription + smsm->local_host);

		ret = smsm_inbound_entry(smsm, entry, node);
		if (ret < 0)
			goto unwind_interfaces;
	}

	platform_set_drvdata(pdev, smsm);

	return 0;

unwind_interfaces:
	for (id = 0; id < smsm->num_entries; id++)
		if (smsm->entries[id].domain)
			irq_domain_remove(smsm->entries[id].domain);

	qcom_smem_state_unregister(smsm->state);

	return ret;
}

static int qcom_smsm_remove(struct platform_device *pdev)
{
	struct qcom_smsm *smsm = platform_get_drvdata(pdev);
	unsigned id;

	for (id = 0; id < smsm->num_entries; id++)
		if (smsm->entries[id].domain)
			irq_domain_remove(smsm->entries[id].domain);

	qcom_smem_state_unregister(smsm->state);

	return 0;
}

static const struct of_device_id qcom_smsm_of_match[] = {
	{ .compatible = "qcom,smsm" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_smsm_of_match);

static struct platform_driver qcom_smsm_driver = {
	.probe = qcom_smsm_probe,
	.remove = qcom_smsm_remove,
	.driver  = {
		.name  = "qcom-smsm",
		.of_match_table = qcom_smsm_of_match,
	},
};
module_platform_driver(qcom_smsm_driver);

MODULE_DESCRIPTION("Qualcomm Shared Memory State Machine driver");
MODULE_LICENSE("GPL v2");
