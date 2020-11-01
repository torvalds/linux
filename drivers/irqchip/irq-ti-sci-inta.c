// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments' K3 Interrupt Aggregator irqchip driver
 *
 * Copyright (C) 2018-2019 Texas Instruments Incorporated - https://www.ti.com/
 *	Lokesh Vutla <lokeshvutla@ti.com>
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/msi.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/soc/ti/ti_sci_inta_msi.h>
#include <linux/soc/ti/ti_sci_protocol.h>
#include <asm-generic/msi.h>

#define TI_SCI_DEV_ID_MASK	0xffff
#define TI_SCI_DEV_ID_SHIFT	16
#define TI_SCI_IRQ_ID_MASK	0xffff
#define TI_SCI_IRQ_ID_SHIFT	0
#define HWIRQ_TO_DEVID(hwirq)	(((hwirq) >> (TI_SCI_DEV_ID_SHIFT)) & \
				 (TI_SCI_DEV_ID_MASK))
#define HWIRQ_TO_IRQID(hwirq)	((hwirq) & (TI_SCI_IRQ_ID_MASK))
#define TO_HWIRQ(dev, index)	((((dev) & TI_SCI_DEV_ID_MASK) << \
				 TI_SCI_DEV_ID_SHIFT) | \
				((index) & TI_SCI_IRQ_ID_MASK))

#define MAX_EVENTS_PER_VINT	64
#define VINT_ENABLE_SET_OFFSET	0x0
#define VINT_ENABLE_CLR_OFFSET	0x8
#define VINT_STATUS_OFFSET	0x18
#define VINT_STATUS_MASKED_OFFSET	0x20

/**
 * struct ti_sci_inta_event_desc - Description of an event coming to
 *				   Interrupt Aggregator. This serves
 *				   as a mapping table for global event,
 *				   hwirq and vint bit.
 * @global_event:	Global event number corresponding to this event
 * @hwirq:		Hwirq of the incoming interrupt
 * @vint_bit:		Corresponding vint bit to which this event is attached.
 */
struct ti_sci_inta_event_desc {
	u16 global_event;
	u32 hwirq;
	u8 vint_bit;
};

/**
 * struct ti_sci_inta_vint_desc - Description of a virtual interrupt coming out
 *				  of Interrupt Aggregator.
 * @domain:		Pointer to IRQ domain to which this vint belongs.
 * @list:		List entry for the vint list
 * @event_map:		Bitmap to manage the allocation of events to vint.
 * @events:		Array of event descriptors assigned to this vint.
 * @parent_virq:	Linux IRQ number that gets attached to parent
 * @vint_id:		TISCI vint ID
 */
struct ti_sci_inta_vint_desc {
	struct irq_domain *domain;
	struct list_head list;
	DECLARE_BITMAP(event_map, MAX_EVENTS_PER_VINT);
	struct ti_sci_inta_event_desc events[MAX_EVENTS_PER_VINT];
	unsigned int parent_virq;
	u16 vint_id;
};

/**
 * struct ti_sci_inta_irq_domain - Structure representing a TISCI based
 *				   Interrupt Aggregator IRQ domain.
 * @sci:		Pointer to TISCI handle
 * @vint:		TISCI resource pointer representing IA inerrupts.
 * @global_event:	TISCI resource pointer representing global events.
 * @vint_list:		List of the vints active in the system
 * @vint_mutex:		Mutex to protect vint_list
 * @base:		Base address of the memory mapped IO registers
 * @pdev:		Pointer to platform device.
 * @ti_sci_id:		TI-SCI device identifier
 * @unmapped_cnt:	Number of @unmapped_dev_ids entries
 * @unmapped_dev_ids:	Pointer to an array of TI-SCI device identifiers of
 *			unmapped event sources.
 *			Unmapped Events are not part of the Global Event Map and
 *			they are converted to Global event within INTA to be
 *			received by the same INTA to generate an interrupt.
 *			In case an interrupt request comes for a device which is
 *			generating Unmapped Event, we must use the INTA's TI-SCI
 *			device identifier in place of the source device
 *			identifier to let sysfw know where it has to program the
 *			Global Event number.
 */
struct ti_sci_inta_irq_domain {
	const struct ti_sci_handle *sci;
	struct ti_sci_resource *vint;
	struct ti_sci_resource *global_event;
	struct list_head vint_list;
	/* Mutex to protect vint list */
	struct mutex vint_mutex;
	void __iomem *base;
	struct platform_device *pdev;
	u32 ti_sci_id;

	int unmapped_cnt;
	u16 *unmapped_dev_ids;
};

#define to_vint_desc(e, i) container_of(e, struct ti_sci_inta_vint_desc, \
					events[i])

static u16 ti_sci_inta_get_dev_id(struct ti_sci_inta_irq_domain *inta, u32 hwirq)
{
	u16 dev_id = HWIRQ_TO_DEVID(hwirq);
	int i;

	if (inta->unmapped_cnt == 0)
		return dev_id;

	/*
	 * For devices sending Unmapped Events we must use the INTA's TI-SCI
	 * device identifier number to be able to convert it to a Global Event
	 * and map it to an interrupt.
	 */
	for (i = 0; i < inta->unmapped_cnt; i++) {
		if (dev_id == inta->unmapped_dev_ids[i]) {
			dev_id = inta->ti_sci_id;
			break;
		}
	}

	return dev_id;
}

/**
 * ti_sci_inta_irq_handler() - Chained IRQ handler for the vint irqs
 * @desc:	Pointer to irq_desc corresponding to the irq
 */
static void ti_sci_inta_irq_handler(struct irq_desc *desc)
{
	struct ti_sci_inta_vint_desc *vint_desc;
	struct ti_sci_inta_irq_domain *inta;
	struct irq_domain *domain;
	unsigned int virq, bit;
	unsigned long val;

	vint_desc = irq_desc_get_handler_data(desc);
	domain = vint_desc->domain;
	inta = domain->host_data;

	chained_irq_enter(irq_desc_get_chip(desc), desc);

	val = readq_relaxed(inta->base + vint_desc->vint_id * 0x1000 +
			    VINT_STATUS_MASKED_OFFSET);

	for_each_set_bit(bit, &val, MAX_EVENTS_PER_VINT) {
		virq = irq_find_mapping(domain, vint_desc->events[bit].hwirq);
		if (virq)
			generic_handle_irq(virq);
	}

	chained_irq_exit(irq_desc_get_chip(desc), desc);
}

/**
 * ti_sci_inta_xlate_irq() - Translate hwirq to parent's hwirq.
 * @inta:	IRQ domain corresponding to Interrupt Aggregator
 * @irq:	Hardware irq corresponding to the above irq domain
 *
 * Return parent irq number if translation is available else -ENOENT.
 */
static int ti_sci_inta_xlate_irq(struct ti_sci_inta_irq_domain *inta,
				 u16 vint_id)
{
	struct device_node *np = dev_of_node(&inta->pdev->dev);
	u32 base, parent_base, size;
	const __be32 *range;
	int len;

	range = of_get_property(np, "ti,interrupt-ranges", &len);
	if (!range)
		return vint_id;

	for (len /= sizeof(*range); len >= 3; len -= 3) {
		base = be32_to_cpu(*range++);
		parent_base = be32_to_cpu(*range++);
		size = be32_to_cpu(*range++);

		if (base <= vint_id && vint_id < base + size)
			return vint_id - base + parent_base;
	}

	return -ENOENT;
}

/**
 * ti_sci_inta_alloc_parent_irq() - Allocate parent irq to Interrupt aggregator
 * @domain:	IRQ domain corresponding to Interrupt Aggregator
 *
 * Return 0 if all went well else corresponding error value.
 */
static struct ti_sci_inta_vint_desc *ti_sci_inta_alloc_parent_irq(struct irq_domain *domain)
{
	struct ti_sci_inta_irq_domain *inta = domain->host_data;
	struct ti_sci_inta_vint_desc *vint_desc;
	struct irq_fwspec parent_fwspec;
	struct device_node *parent_node;
	unsigned int parent_virq;
	int p_hwirq, ret;
	u16 vint_id;

	vint_id = ti_sci_get_free_resource(inta->vint);
	if (vint_id == TI_SCI_RESOURCE_NULL)
		return ERR_PTR(-EINVAL);

	p_hwirq = ti_sci_inta_xlate_irq(inta, vint_id);
	if (p_hwirq < 0) {
		ret = p_hwirq;
		goto free_vint;
	}

	vint_desc = kzalloc(sizeof(*vint_desc), GFP_KERNEL);
	if (!vint_desc) {
		ret = -ENOMEM;
		goto free_vint;
	}

	vint_desc->domain = domain;
	vint_desc->vint_id = vint_id;
	INIT_LIST_HEAD(&vint_desc->list);

	parent_node = of_irq_find_parent(dev_of_node(&inta->pdev->dev));
	parent_fwspec.fwnode = of_node_to_fwnode(parent_node);

	if (of_device_is_compatible(parent_node, "arm,gic-v3")) {
		/* Parent is GIC */
		parent_fwspec.param_count = 3;
		parent_fwspec.param[0] = 0;
		parent_fwspec.param[1] = p_hwirq - 32;
		parent_fwspec.param[2] = IRQ_TYPE_LEVEL_HIGH;
	} else {
		/* Parent is Interrupt Router */
		parent_fwspec.param_count = 1;
		parent_fwspec.param[0] = p_hwirq;
	}

	parent_virq = irq_create_fwspec_mapping(&parent_fwspec);
	if (parent_virq == 0) {
		dev_err(&inta->pdev->dev, "Parent IRQ allocation failed\n");
		ret = -EINVAL;
		goto free_vint_desc;

	}
	vint_desc->parent_virq = parent_virq;

	list_add_tail(&vint_desc->list, &inta->vint_list);
	irq_set_chained_handler_and_data(vint_desc->parent_virq,
					 ti_sci_inta_irq_handler, vint_desc);

	return vint_desc;
free_vint_desc:
	kfree(vint_desc);
free_vint:
	ti_sci_release_resource(inta->vint, vint_id);
	return ERR_PTR(ret);
}

/**
 * ti_sci_inta_alloc_event() - Attach an event to a IA vint.
 * @vint_desc:	Pointer to vint_desc to which the event gets attached
 * @free_bit:	Bit inside vint to which event gets attached
 * @hwirq:	hwirq of the input event
 *
 * Return event_desc pointer if all went ok else appropriate error value.
 */
static struct ti_sci_inta_event_desc *ti_sci_inta_alloc_event(struct ti_sci_inta_vint_desc *vint_desc,
							      u16 free_bit,
							      u32 hwirq)
{
	struct ti_sci_inta_irq_domain *inta = vint_desc->domain->host_data;
	struct ti_sci_inta_event_desc *event_desc;
	u16 dev_id, dev_index;
	int err;

	dev_id = ti_sci_inta_get_dev_id(inta, hwirq);
	dev_index = HWIRQ_TO_IRQID(hwirq);

	event_desc = &vint_desc->events[free_bit];
	event_desc->hwirq = hwirq;
	event_desc->vint_bit = free_bit;
	event_desc->global_event = ti_sci_get_free_resource(inta->global_event);
	if (event_desc->global_event == TI_SCI_RESOURCE_NULL)
		return ERR_PTR(-EINVAL);

	err = inta->sci->ops.rm_irq_ops.set_event_map(inta->sci,
						      dev_id, dev_index,
						      inta->ti_sci_id,
						      vint_desc->vint_id,
						      event_desc->global_event,
						      free_bit);
	if (err)
		goto free_global_event;

	return event_desc;
free_global_event:
	ti_sci_release_resource(inta->global_event, event_desc->global_event);
	return ERR_PTR(err);
}

/**
 * ti_sci_inta_alloc_irq() -  Allocate an irq within INTA domain
 * @domain:	irq_domain pointer corresponding to INTA
 * @hwirq:	hwirq of the input event
 *
 * Note: Allocation happens in the following manner:
 *	- Find a free bit available in any of the vints available in the list.
 *	- If not found, allocate a vint from the vint pool
 *	- Attach the free bit to input hwirq.
 * Return event_desc if all went ok else appropriate error value.
 */
static struct ti_sci_inta_event_desc *ti_sci_inta_alloc_irq(struct irq_domain *domain,
							    u32 hwirq)
{
	struct ti_sci_inta_irq_domain *inta = domain->host_data;
	struct ti_sci_inta_vint_desc *vint_desc = NULL;
	struct ti_sci_inta_event_desc *event_desc;
	u16 free_bit;

	mutex_lock(&inta->vint_mutex);
	list_for_each_entry(vint_desc, &inta->vint_list, list) {
		free_bit = find_first_zero_bit(vint_desc->event_map,
					       MAX_EVENTS_PER_VINT);
		if (free_bit != MAX_EVENTS_PER_VINT) {
			set_bit(free_bit, vint_desc->event_map);
			goto alloc_event;
		}
	}

	/* No free bits available. Allocate a new vint */
	vint_desc = ti_sci_inta_alloc_parent_irq(domain);
	if (IS_ERR(vint_desc)) {
		event_desc = ERR_CAST(vint_desc);
		goto unlock;
	}

	free_bit = find_first_zero_bit(vint_desc->event_map,
				       MAX_EVENTS_PER_VINT);
	set_bit(free_bit, vint_desc->event_map);

alloc_event:
	event_desc = ti_sci_inta_alloc_event(vint_desc, free_bit, hwirq);
	if (IS_ERR(event_desc))
		clear_bit(free_bit, vint_desc->event_map);

unlock:
	mutex_unlock(&inta->vint_mutex);
	return event_desc;
}

/**
 * ti_sci_inta_free_parent_irq() - Free a parent irq to INTA
 * @inta:	Pointer to inta domain.
 * @vint_desc:	Pointer to vint_desc that needs to be freed.
 */
static void ti_sci_inta_free_parent_irq(struct ti_sci_inta_irq_domain *inta,
					struct ti_sci_inta_vint_desc *vint_desc)
{
	if (find_first_bit(vint_desc->event_map, MAX_EVENTS_PER_VINT) == MAX_EVENTS_PER_VINT) {
		list_del(&vint_desc->list);
		ti_sci_release_resource(inta->vint, vint_desc->vint_id);
		irq_dispose_mapping(vint_desc->parent_virq);
		kfree(vint_desc);
	}
}

/**
 * ti_sci_inta_free_irq() - Free an IRQ within INTA domain
 * @event_desc:	Pointer to event_desc that needs to be freed.
 * @hwirq:	Hwirq number within INTA domain that needs to be freed
 */
static void ti_sci_inta_free_irq(struct ti_sci_inta_event_desc *event_desc,
				 u32 hwirq)
{
	struct ti_sci_inta_vint_desc *vint_desc;
	struct ti_sci_inta_irq_domain *inta;
	u16 dev_id;

	vint_desc = to_vint_desc(event_desc, event_desc->vint_bit);
	inta = vint_desc->domain->host_data;
	dev_id = ti_sci_inta_get_dev_id(inta, hwirq);
	/* free event irq */
	mutex_lock(&inta->vint_mutex);
	inta->sci->ops.rm_irq_ops.free_event_map(inta->sci,
						 dev_id, HWIRQ_TO_IRQID(hwirq),
						 inta->ti_sci_id,
						 vint_desc->vint_id,
						 event_desc->global_event,
						 event_desc->vint_bit);

	clear_bit(event_desc->vint_bit, vint_desc->event_map);
	ti_sci_release_resource(inta->global_event, event_desc->global_event);
	event_desc->global_event = TI_SCI_RESOURCE_NULL;
	event_desc->hwirq = 0;

	ti_sci_inta_free_parent_irq(inta, vint_desc);
	mutex_unlock(&inta->vint_mutex);
}

/**
 * ti_sci_inta_request_resources() - Allocate resources for input irq
 * @data: Pointer to corresponding irq_data
 *
 * Note: This is the core api where the actual allocation happens for input
 *	 hwirq. This allocation involves creating a parent irq for vint.
 *	 If this is done in irq_domain_ops.alloc() then a deadlock is reached
 *	 for allocation. So this allocation is being done in request_resources()
 *
 * Return: 0 if all went well else corresponding error.
 */
static int ti_sci_inta_request_resources(struct irq_data *data)
{
	struct ti_sci_inta_event_desc *event_desc;

	event_desc = ti_sci_inta_alloc_irq(data->domain, data->hwirq);
	if (IS_ERR(event_desc))
		return PTR_ERR(event_desc);

	data->chip_data = event_desc;

	return 0;
}

/**
 * ti_sci_inta_release_resources - Release resources for input irq
 * @data: Pointer to corresponding irq_data
 *
 * Note: Corresponding to request_resources(), all the unmapping and deletion
 *	 of parent vint irqs happens in this api.
 */
static void ti_sci_inta_release_resources(struct irq_data *data)
{
	struct ti_sci_inta_event_desc *event_desc;

	event_desc = irq_data_get_irq_chip_data(data);
	ti_sci_inta_free_irq(event_desc, data->hwirq);
}

/**
 * ti_sci_inta_manage_event() - Control the event based on the offset
 * @data:	Pointer to corresponding irq_data
 * @offset:	register offset using which event is controlled.
 */
static void ti_sci_inta_manage_event(struct irq_data *data, u32 offset)
{
	struct ti_sci_inta_event_desc *event_desc;
	struct ti_sci_inta_vint_desc *vint_desc;
	struct ti_sci_inta_irq_domain *inta;

	event_desc = irq_data_get_irq_chip_data(data);
	vint_desc = to_vint_desc(event_desc, event_desc->vint_bit);
	inta = data->domain->host_data;

	writeq_relaxed(BIT(event_desc->vint_bit),
		       inta->base + vint_desc->vint_id * 0x1000 + offset);
}

/**
 * ti_sci_inta_mask_irq() - Mask an event
 * @data:	Pointer to corresponding irq_data
 */
static void ti_sci_inta_mask_irq(struct irq_data *data)
{
	ti_sci_inta_manage_event(data, VINT_ENABLE_CLR_OFFSET);
}

/**
 * ti_sci_inta_unmask_irq() - Unmask an event
 * @data:	Pointer to corresponding irq_data
 */
static void ti_sci_inta_unmask_irq(struct irq_data *data)
{
	ti_sci_inta_manage_event(data, VINT_ENABLE_SET_OFFSET);
}

/**
 * ti_sci_inta_ack_irq() - Ack an event
 * @data:	Pointer to corresponding irq_data
 */
static void ti_sci_inta_ack_irq(struct irq_data *data)
{
	/*
	 * Do not clear the event if hardware is capable of sending
	 * a down event.
	 */
	if (irqd_get_trigger_type(data) != IRQF_TRIGGER_HIGH)
		ti_sci_inta_manage_event(data, VINT_STATUS_OFFSET);
}

static int ti_sci_inta_set_affinity(struct irq_data *d,
				    const struct cpumask *mask_val, bool force)
{
	return -EINVAL;
}

/**
 * ti_sci_inta_set_type() - Update the trigger type of the irq.
 * @data:	Pointer to corresponding irq_data
 * @type:	Trigger type as specified by user
 *
 * Note: This updates the handle_irq callback for level msi.
 *
 * Return 0 if all went well else appropriate error.
 */
static int ti_sci_inta_set_type(struct irq_data *data, unsigned int type)
{
	/*
	 * .alloc default sets handle_edge_irq. But if the user specifies
	 * that IRQ is level MSI, then update the handle to handle_level_irq
	 */
	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQF_TRIGGER_HIGH:
		irq_set_handler_locked(data, handle_level_irq);
		return 0;
	case IRQF_TRIGGER_RISING:
		return 0;
	default:
		return -EINVAL;
	}
}

static struct irq_chip ti_sci_inta_irq_chip = {
	.name			= "INTA",
	.irq_ack		= ti_sci_inta_ack_irq,
	.irq_mask		= ti_sci_inta_mask_irq,
	.irq_set_type		= ti_sci_inta_set_type,
	.irq_unmask		= ti_sci_inta_unmask_irq,
	.irq_set_affinity	= ti_sci_inta_set_affinity,
	.irq_request_resources	= ti_sci_inta_request_resources,
	.irq_release_resources	= ti_sci_inta_release_resources,
};

/**
 * ti_sci_inta_irq_domain_free() - Free an IRQ from the IRQ domain
 * @domain:	Domain to which the irqs belong
 * @virq:	base linux virtual IRQ to be freed.
 * @nr_irqs:	Number of continuous irqs to be freed
 */
static void ti_sci_inta_irq_domain_free(struct irq_domain *domain,
					unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);

	irq_domain_reset_irq_data(data);
}

/**
 * ti_sci_inta_irq_domain_alloc() - Allocate Interrupt aggregator IRQs
 * @domain:	Point to the interrupt aggregator IRQ domain
 * @virq:	Corresponding Linux virtual IRQ number
 * @nr_irqs:	Continuous irqs to be allocated
 * @data:	Pointer to firmware specifier
 *
 * No actual allocation happens here.
 *
 * Return 0 if all went well else appropriate error value.
 */
static int ti_sci_inta_irq_domain_alloc(struct irq_domain *domain,
					unsigned int virq, unsigned int nr_irqs,
					void *data)
{
	msi_alloc_info_t *arg = data;

	irq_domain_set_info(domain, virq, arg->hwirq, &ti_sci_inta_irq_chip,
			    NULL, handle_edge_irq, NULL, NULL);

	return 0;
}

static const struct irq_domain_ops ti_sci_inta_irq_domain_ops = {
	.free		= ti_sci_inta_irq_domain_free,
	.alloc		= ti_sci_inta_irq_domain_alloc,
};

static struct irq_chip ti_sci_inta_msi_irq_chip = {
	.name			= "MSI-INTA",
	.flags			= IRQCHIP_SUPPORTS_LEVEL_MSI,
};

static void ti_sci_inta_msi_set_desc(msi_alloc_info_t *arg,
				     struct msi_desc *desc)
{
	struct platform_device *pdev = to_platform_device(desc->dev);

	arg->desc = desc;
	arg->hwirq = TO_HWIRQ(pdev->id, desc->inta.dev_index);
}

static struct msi_domain_ops ti_sci_inta_msi_ops = {
	.set_desc	= ti_sci_inta_msi_set_desc,
};

static struct msi_domain_info ti_sci_inta_msi_domain_info = {
	.flags	= (MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		   MSI_FLAG_LEVEL_CAPABLE),
	.ops	= &ti_sci_inta_msi_ops,
	.chip	= &ti_sci_inta_msi_irq_chip,
};

static int ti_sci_inta_get_unmapped_sources(struct ti_sci_inta_irq_domain *inta)
{
	struct device *dev = &inta->pdev->dev;
	struct device_node *node = dev_of_node(dev);
	struct of_phandle_iterator it;
	int count, err, ret, i;

	count = of_count_phandle_with_args(node, "ti,unmapped-event-sources", NULL);
	if (count <= 0)
		return 0;

	inta->unmapped_dev_ids = devm_kcalloc(dev, count,
					      sizeof(*inta->unmapped_dev_ids),
					      GFP_KERNEL);
	if (!inta->unmapped_dev_ids)
		return -ENOMEM;

	i = 0;
	of_for_each_phandle(&it, err, node, "ti,unmapped-event-sources", NULL, 0) {
		u32 dev_id;

		ret = of_property_read_u32(it.node, "ti,sci-dev-id", &dev_id);
		if (ret) {
			dev_err(dev, "ti,sci-dev-id read failure for %pOFf\n", it.node);
			of_node_put(it.node);
			return ret;
		}
		inta->unmapped_dev_ids[i++] = dev_id;
	}

	inta->unmapped_cnt = count;

	return 0;
}

static int ti_sci_inta_irq_domain_probe(struct platform_device *pdev)
{
	struct irq_domain *parent_domain, *domain, *msi_domain;
	struct device_node *parent_node, *node;
	struct ti_sci_inta_irq_domain *inta;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	node = dev_of_node(dev);
	parent_node = of_irq_find_parent(node);
	if (!parent_node) {
		dev_err(dev, "Failed to get IRQ parent node\n");
		return -ENODEV;
	}

	parent_domain = irq_find_host(parent_node);
	if (!parent_domain)
		return -EPROBE_DEFER;

	inta = devm_kzalloc(dev, sizeof(*inta), GFP_KERNEL);
	if (!inta)
		return -ENOMEM;

	inta->pdev = pdev;
	inta->sci = devm_ti_sci_get_by_phandle(dev, "ti,sci");
	if (IS_ERR(inta->sci))
		return dev_err_probe(dev, PTR_ERR(inta->sci),
				     "ti,sci read fail\n");

	ret = of_property_read_u32(dev->of_node, "ti,sci-dev-id", &inta->ti_sci_id);
	if (ret) {
		dev_err(dev, "missing 'ti,sci-dev-id' property\n");
		return -EINVAL;
	}

	inta->vint = devm_ti_sci_get_resource(inta->sci, dev, inta->ti_sci_id,
					      TI_SCI_RESASG_SUBTYPE_IA_VINT);
	if (IS_ERR(inta->vint)) {
		dev_err(dev, "VINT resource allocation failed\n");
		return PTR_ERR(inta->vint);
	}

	inta->global_event = devm_ti_sci_get_resource(inta->sci, dev, inta->ti_sci_id,
						      TI_SCI_RESASG_SUBTYPE_GLOBAL_EVENT_SEVT);
	if (IS_ERR(inta->global_event)) {
		dev_err(dev, "Global event resource allocation failed\n");
		return PTR_ERR(inta->global_event);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	inta->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(inta->base))
		return PTR_ERR(inta->base);

	ret = ti_sci_inta_get_unmapped_sources(inta);
	if (ret)
		return ret;

	domain = irq_domain_add_linear(dev_of_node(dev),
				       ti_sci_get_num_resources(inta->vint),
				       &ti_sci_inta_irq_domain_ops, inta);
	if (!domain) {
		dev_err(dev, "Failed to allocate IRQ domain\n");
		return -ENOMEM;
	}

	msi_domain = ti_sci_inta_msi_create_irq_domain(of_node_to_fwnode(node),
						&ti_sci_inta_msi_domain_info,
						domain);
	if (!msi_domain) {
		irq_domain_remove(domain);
		dev_err(dev, "Failed to allocate msi domain\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&inta->vint_list);
	mutex_init(&inta->vint_mutex);

	dev_info(dev, "Interrupt Aggregator domain %d created\n", pdev->id);

	return 0;
}

static const struct of_device_id ti_sci_inta_irq_domain_of_match[] = {
	{ .compatible = "ti,sci-inta", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ti_sci_inta_irq_domain_of_match);

static struct platform_driver ti_sci_inta_irq_domain_driver = {
	.probe = ti_sci_inta_irq_domain_probe,
	.driver = {
		.name = "ti-sci-inta",
		.of_match_table = ti_sci_inta_irq_domain_of_match,
	},
};
module_platform_driver(ti_sci_inta_irq_domain_driver);

MODULE_AUTHOR("Lokesh Vutla <lokeshvutla@ti.com>");
MODULE_DESCRIPTION("K3 Interrupt Aggregator driver over TI SCI protocol");
MODULE_LICENSE("GPL v2");
