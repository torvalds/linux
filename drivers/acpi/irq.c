// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI GSI IRQ layer
 *
 * Copyright (C) 2015 ARM Ltd.
 * Author: Lorenzo Pieralisi <lorenzo.pieralisi@arm.com>
 */
#include <linux/acpi.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>

enum acpi_irq_model_id acpi_irq_model;

static acpi_gsi_domain_disp_fn acpi_get_gsi_domain_id;
static u32 (*acpi_gsi_to_irq_fallback)(u32 gsi);

/**
 * acpi_gsi_to_irq() - Retrieve the linux irq number for a given GSI
 * @gsi: GSI IRQ number to map
 * @irq: pointer where linux IRQ number is stored
 *
 * irq location updated with irq value [>0 on success, 0 on failure]
 *
 * Returns: 0 on success
 *          -EINVAL on failure
 */
int acpi_gsi_to_irq(u32 gsi, unsigned int *irq)
{
	struct irq_domain *d;

	d = irq_find_matching_fwnode(acpi_get_gsi_domain_id(gsi),
					DOMAIN_BUS_ANY);
	*irq = irq_find_mapping(d, gsi);
	/*
	 * *irq == 0 means no mapping, that should be reported as a
	 * failure, unless there is an arch-specific fallback handler.
	 */
	if (!*irq && acpi_gsi_to_irq_fallback)
		*irq = acpi_gsi_to_irq_fallback(gsi);

	return (*irq > 0) ? 0 : -EINVAL;
}
EXPORT_SYMBOL_GPL(acpi_gsi_to_irq);

/**
 * acpi_register_gsi() - Map a GSI to a linux IRQ number
 * @dev: device for which IRQ has to be mapped
 * @gsi: GSI IRQ number
 * @trigger: trigger type of the GSI number to be mapped
 * @polarity: polarity of the GSI to be mapped
 *
 * Returns: a valid linux IRQ number on success
 *          -EINVAL on failure
 */
int acpi_register_gsi(struct device *dev, u32 gsi, int trigger,
		      int polarity)
{
	struct irq_fwspec fwspec;
	unsigned int irq;

	fwspec.fwnode = acpi_get_gsi_domain_id(gsi);
	if (WARN_ON(!fwspec.fwnode)) {
		pr_warn("GSI: No registered irqchip, giving up\n");
		return -EINVAL;
	}

	fwspec.param[0] = gsi;
	fwspec.param[1] = acpi_dev_get_irq_type(trigger, polarity);
	fwspec.param_count = 2;

	irq = irq_create_fwspec_mapping(&fwspec);
	if (!irq)
		return -EINVAL;

	return irq;
}
EXPORT_SYMBOL_GPL(acpi_register_gsi);

/**
 * acpi_unregister_gsi() - Free a GSI<->linux IRQ number mapping
 * @gsi: GSI IRQ number
 */
void acpi_unregister_gsi(u32 gsi)
{
	struct irq_domain *d;
	int irq;

	if (WARN_ON(acpi_irq_model == ACPI_IRQ_MODEL_GIC && gsi < 16))
		return;

	d = irq_find_matching_fwnode(acpi_get_gsi_domain_id(gsi),
				     DOMAIN_BUS_ANY);
	irq = irq_find_mapping(d, gsi);
	irq_dispose_mapping(irq);
}
EXPORT_SYMBOL_GPL(acpi_unregister_gsi);

/**
 * acpi_get_irq_source_fwhandle() - Retrieve fwhandle from IRQ resource source.
 * @source: acpi_resource_source to use for the lookup.
 * @gsi: GSI IRQ number
 *
 * Description:
 * Retrieve the fwhandle of the device referenced by the given IRQ resource
 * source.
 *
 * Return:
 * The referenced device fwhandle or NULL on failure
 */
static struct fwnode_handle *
acpi_get_irq_source_fwhandle(const struct acpi_resource_source *source,
			     u32 gsi)
{
	struct fwnode_handle *result;
	struct acpi_device *device;
	acpi_handle handle;
	acpi_status status;

	if (!source->string_length)
		return acpi_get_gsi_domain_id(gsi);

	status = acpi_get_handle(NULL, source->string_ptr, &handle);
	if (WARN_ON(ACPI_FAILURE(status)))
		return NULL;

	device = acpi_get_acpi_dev(handle);
	if (WARN_ON(!device))
		return NULL;

	result = &device->fwnode;
	acpi_put_acpi_dev(device);
	return result;
}

/*
 * Context for the resource walk used to lookup IRQ resources.
 * Contains a return code, the lookup index, and references to the flags
 * and fwspec where the result is returned.
 */
struct acpi_irq_parse_one_ctx {
	int rc;
	unsigned int index;
	unsigned long *res_flags;
	struct irq_fwspec *fwspec;
};

/**
 * acpi_irq_parse_one_match - Handle a matching IRQ resource.
 * @fwnode: matching fwnode
 * @hwirq: hardware IRQ number
 * @triggering: triggering attributes of hwirq
 * @polarity: polarity attributes of hwirq
 * @polarity: polarity attributes of hwirq
 * @shareable: shareable attributes of hwirq
 * @wake_capable: wake capable attribute of hwirq
 * @ctx: acpi_irq_parse_one_ctx updated by this function
 *
 * Description:
 * Handle a matching IRQ resource by populating the given ctx with
 * the information passed.
 */
static inline void acpi_irq_parse_one_match(struct fwnode_handle *fwnode,
					    u32 hwirq, u8 triggering,
					    u8 polarity, u8 shareable,
					    u8 wake_capable,
					    struct acpi_irq_parse_one_ctx *ctx)
{
	if (!fwnode)
		return;
	ctx->rc = 0;
	*ctx->res_flags = acpi_dev_irq_flags(triggering, polarity, shareable, wake_capable);
	ctx->fwspec->fwnode = fwnode;
	ctx->fwspec->param[0] = hwirq;
	ctx->fwspec->param[1] = acpi_dev_get_irq_type(triggering, polarity);
	ctx->fwspec->param_count = 2;
}

/**
 * acpi_irq_parse_one_cb - Handle the given resource.
 * @ares: resource to handle
 * @context: context for the walk
 *
 * Description:
 * This is called by acpi_walk_resources passing each resource returned by
 * the _CRS method. We only inspect IRQ resources. Since IRQ resources
 * might contain multiple interrupts we check if the index is within this
 * one's interrupt array, otherwise we subtract the current resource IRQ
 * count from the lookup index to prepare for the next resource.
 * Once a match is found we call acpi_irq_parse_one_match to populate
 * the result and end the walk by returning AE_CTRL_TERMINATE.
 *
 * Return:
 * AE_OK if the walk should continue, AE_CTRL_TERMINATE if a matching
 * IRQ resource was found.
 */
static acpi_status acpi_irq_parse_one_cb(struct acpi_resource *ares,
					 void *context)
{
	struct acpi_irq_parse_one_ctx *ctx = context;
	struct acpi_resource_irq *irq;
	struct acpi_resource_extended_irq *eirq;
	struct fwnode_handle *fwnode;

	switch (ares->type) {
	case ACPI_RESOURCE_TYPE_IRQ:
		irq = &ares->data.irq;
		if (ctx->index >= irq->interrupt_count) {
			ctx->index -= irq->interrupt_count;
			return AE_OK;
		}
		fwnode = acpi_get_gsi_domain_id(irq->interrupts[ctx->index]);
		acpi_irq_parse_one_match(fwnode, irq->interrupts[ctx->index],
					 irq->triggering, irq->polarity,
					 irq->shareable, irq->wake_capable, ctx);
		return AE_CTRL_TERMINATE;
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		eirq = &ares->data.extended_irq;
		if (eirq->producer_consumer == ACPI_PRODUCER)
			return AE_OK;
		if (ctx->index >= eirq->interrupt_count) {
			ctx->index -= eirq->interrupt_count;
			return AE_OK;
		}
		fwnode = acpi_get_irq_source_fwhandle(&eirq->resource_source,
						      eirq->interrupts[ctx->index]);
		acpi_irq_parse_one_match(fwnode, eirq->interrupts[ctx->index],
					 eirq->triggering, eirq->polarity,
					 eirq->shareable, eirq->wake_capable, ctx);
		return AE_CTRL_TERMINATE;
	}

	return AE_OK;
}

/**
 * acpi_irq_parse_one - Resolve an interrupt for a device
 * @handle: the device whose interrupt is to be resolved
 * @index: index of the interrupt to resolve
 * @fwspec: structure irq_fwspec filled by this function
 * @flags: resource flags filled by this function
 *
 * Description:
 * Resolves an interrupt for a device by walking its CRS resources to find
 * the appropriate ACPI IRQ resource and populating the given struct irq_fwspec
 * and flags.
 *
 * Return:
 * The result stored in ctx.rc by the callback, or the default -EINVAL value
 * if an error occurs.
 */
static int acpi_irq_parse_one(acpi_handle handle, unsigned int index,
			      struct irq_fwspec *fwspec, unsigned long *flags)
{
	struct acpi_irq_parse_one_ctx ctx = { -EINVAL, index, flags, fwspec };

	acpi_walk_resources(handle, METHOD_NAME__CRS, acpi_irq_parse_one_cb, &ctx);
	return ctx.rc;
}

/**
 * acpi_irq_get - Lookup an ACPI IRQ resource and use it to initialize resource.
 * @handle: ACPI device handle
 * @index:  ACPI IRQ resource index to lookup
 * @res:    Linux IRQ resource to initialize
 *
 * Description:
 * Look for the ACPI IRQ resource with the given index and use it to initialize
 * the given Linux IRQ resource.
 *
 * Return:
 * 0 on success
 * -EINVAL if an error occurs
 * -EPROBE_DEFER if the IRQ lookup/conversion failed
 */
int acpi_irq_get(acpi_handle handle, unsigned int index, struct resource *res)
{
	struct irq_fwspec fwspec;
	struct irq_domain *domain;
	unsigned long flags;
	int rc;

	rc = acpi_irq_parse_one(handle, index, &fwspec, &flags);
	if (rc)
		return rc;

	domain = irq_find_matching_fwnode(fwspec.fwnode, DOMAIN_BUS_ANY);
	if (!domain)
		return -EPROBE_DEFER;

	rc = irq_create_fwspec_mapping(&fwspec);
	if (rc <= 0)
		return -EINVAL;

	res->start = rc;
	res->end = rc;
	res->flags = flags;

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_irq_get);

/**
 * acpi_set_irq_model - Setup the GSI irqdomain information
 * @model: the value assigned to acpi_irq_model
 * @fn: a dispatcher function that will return the domain fwnode
 *	for a given GSI
 */
void __init acpi_set_irq_model(enum acpi_irq_model_id model,
			       acpi_gsi_domain_disp_fn fn)
{
	acpi_irq_model = model;
	acpi_get_gsi_domain_id = fn;
}

/*
 * acpi_get_gsi_dispatcher() - Get the GSI dispatcher function
 *
 * Return the dispatcher function that computes the domain fwnode for
 * a given GSI.
 */
acpi_gsi_domain_disp_fn acpi_get_gsi_dispatcher(void)
{
	return acpi_get_gsi_domain_id;
}
EXPORT_SYMBOL_GPL(acpi_get_gsi_dispatcher);

/**
 * acpi_set_gsi_to_irq_fallback - Register a GSI transfer
 * callback to fallback to arch specified implementation.
 * @fn: arch-specific fallback handler
 */
void __init acpi_set_gsi_to_irq_fallback(u32 (*fn)(u32))
{
	acpi_gsi_to_irq_fallback = fn;
}

/**
 * acpi_irq_create_hierarchy - Create a hierarchical IRQ domain with the default
 *                             GSI domain as its parent.
 * @flags:      Irq domain flags associated with the domain
 * @size:       Size of the domain.
 * @fwnode:     Optional fwnode of the interrupt controller
 * @ops:        Pointer to the interrupt domain callbacks
 * @host_data:  Controller private data pointer
 */
struct irq_domain *acpi_irq_create_hierarchy(unsigned int flags,
					     unsigned int size,
					     struct fwnode_handle *fwnode,
					     const struct irq_domain_ops *ops,
					     void *host_data)
{
	struct irq_domain *d;

	/* This only works for the GIC model... */
	if (acpi_irq_model != ACPI_IRQ_MODEL_GIC)
		return NULL;

	d = irq_find_matching_fwnode(acpi_get_gsi_domain_id(0),
				     DOMAIN_BUS_ANY);

	if (!d)
		return NULL;

	return irq_domain_create_hierarchy(d, flags, size, fwnode, ops,
					   host_data);
}
EXPORT_SYMBOL_GPL(acpi_irq_create_hierarchy);
