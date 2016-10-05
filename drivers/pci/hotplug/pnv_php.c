/*
 * PCI Hotplug Driver for PowerPC PowerNV platform.
 *
 * Copyright Gavin Shan, IBM Corporation 2016.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/libfdt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>

#include <asm/opal.h>
#include <asm/pnv-pci.h>
#include <asm/ppc-pci.h>

#define DRIVER_VERSION	"0.1"
#define DRIVER_AUTHOR	"Gavin Shan, IBM Corporation"
#define DRIVER_DESC	"PowerPC PowerNV PCI Hotplug Driver"

static LIST_HEAD(pnv_php_slot_list);
static DEFINE_SPINLOCK(pnv_php_lock);

static void pnv_php_register(struct device_node *dn);
static void pnv_php_unregister_one(struct device_node *dn);
static void pnv_php_unregister(struct device_node *dn);

static void pnv_php_free_slot(struct kref *kref)
{
	struct pnv_php_slot *php_slot = container_of(kref,
					struct pnv_php_slot, kref);

	WARN_ON(!list_empty(&php_slot->children));
	kfree(php_slot->name);
	kfree(php_slot);
}

static inline void pnv_php_put_slot(struct pnv_php_slot *php_slot)
{

	if (WARN_ON(!php_slot))
		return;

	kref_put(&php_slot->kref, pnv_php_free_slot);
}

static struct pnv_php_slot *pnv_php_match(struct device_node *dn,
					  struct pnv_php_slot *php_slot)
{
	struct pnv_php_slot *target, *tmp;

	if (php_slot->dn == dn) {
		kref_get(&php_slot->kref);
		return php_slot;
	}

	list_for_each_entry(tmp, &php_slot->children, link) {
		target = pnv_php_match(dn, tmp);
		if (target)
			return target;
	}

	return NULL;
}

struct pnv_php_slot *pnv_php_find_slot(struct device_node *dn)
{
	struct pnv_php_slot *php_slot, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&pnv_php_lock, flags);
	list_for_each_entry(tmp, &pnv_php_slot_list, link) {
		php_slot = pnv_php_match(dn, tmp);
		if (php_slot) {
			spin_unlock_irqrestore(&pnv_php_lock, flags);
			return php_slot;
		}
	}
	spin_unlock_irqrestore(&pnv_php_lock, flags);

	return NULL;
}
EXPORT_SYMBOL_GPL(pnv_php_find_slot);

/*
 * Remove pdn for all children of the indicated device node.
 * The function should remove pdn in a depth-first manner.
 */
static void pnv_php_rmv_pdns(struct device_node *dn)
{
	struct device_node *child;

	for_each_child_of_node(dn, child) {
		pnv_php_rmv_pdns(child);

		pci_remove_device_node_info(child);
	}
}

/*
 * Detach all child nodes of the indicated device nodes. The
 * function should handle device nodes in depth-first manner.
 *
 * We should not invoke of_node_release() as the memory for
 * individual device node is part of large memory block. The
 * large block is allocated from memblock (system bootup) or
 * kmalloc() when unflattening the device tree by OF changeset.
 * We can not free the large block allocated from memblock. For
 * later case, it should be released at once.
 */
static void pnv_php_detach_device_nodes(struct device_node *parent)
{
	struct device_node *dn;
	int refcount;

	for_each_child_of_node(parent, dn) {
		pnv_php_detach_device_nodes(dn);

		of_node_put(dn);
		refcount = atomic_read(&dn->kobj.kref.refcount);
		if (unlikely(refcount != 1))
			pr_warn("Invalid refcount %d on <%s>\n",
				refcount, of_node_full_name(dn));

		of_detach_node(dn);
	}
}

static void pnv_php_rmv_devtree(struct pnv_php_slot *php_slot)
{
	pnv_php_rmv_pdns(php_slot->dn);

	/*
	 * Decrease the refcount if the device nodes were created
	 * through OF changeset before detaching them.
	 */
	if (php_slot->fdt)
		of_changeset_destroy(&php_slot->ocs);
	pnv_php_detach_device_nodes(php_slot->dn);

	if (php_slot->fdt) {
		kfree(php_slot->dt);
		kfree(php_slot->fdt);
		php_slot->dt        = NULL;
		php_slot->dn->child = NULL;
		php_slot->fdt       = NULL;
	}
}

/*
 * As the nodes in OF changeset are applied in reverse order, we
 * need revert the nodes in advance so that we have correct node
 * order after the changeset is applied.
 */
static void pnv_php_reverse_nodes(struct device_node *parent)
{
	struct device_node *child, *next;

	/* In-depth first */
	for_each_child_of_node(parent, child)
		pnv_php_reverse_nodes(child);

	/* Reverse the nodes in the child list */
	child = parent->child;
	parent->child = NULL;
	while (child) {
		next = child->sibling;

		child->sibling = parent->child;
		parent->child = child;
		child = next;
	}
}

static int pnv_php_populate_changeset(struct of_changeset *ocs,
				      struct device_node *dn)
{
	struct device_node *child;
	int ret = 0;

	for_each_child_of_node(dn, child) {
		ret = of_changeset_attach_node(ocs, child);
		if (unlikely(ret))
			break;

		ret = pnv_php_populate_changeset(ocs, child);
		if (unlikely(ret))
			break;
	}

	return ret;
}

static void *pnv_php_add_one_pdn(struct device_node *dn, void *data)
{
	struct pci_controller *hose = (struct pci_controller *)data;
	struct pci_dn *pdn;

	pdn = pci_add_device_node_info(hose, dn);
	if (unlikely(!pdn))
		return ERR_PTR(-ENOMEM);

	return NULL;
}

static void pnv_php_add_pdns(struct pnv_php_slot *slot)
{
	struct pci_controller *hose = pci_bus_to_host(slot->bus);

	pci_traverse_device_nodes(slot->dn, pnv_php_add_one_pdn, hose);
}

static int pnv_php_add_devtree(struct pnv_php_slot *php_slot)
{
	void *fdt, *fdt1, *dt;
	int ret;

	/* We don't know the FDT blob size. We try to get it through
	 * maximal memory chunk and then copy it to another chunk that
	 * fits the real size.
	 */
	fdt1 = kzalloc(0x10000, GFP_KERNEL);
	if (unlikely(!fdt1)) {
		ret = -ENOMEM;
		dev_warn(&php_slot->pdev->dev, "Cannot alloc FDT blob\n");
		goto out;
	}

	ret = pnv_pci_get_device_tree(php_slot->dn->phandle, fdt1, 0x10000);
	if (unlikely(ret)) {
		dev_warn(&php_slot->pdev->dev, "Error %d getting FDT blob\n",
			 ret);
		goto free_fdt1;
	}

	fdt = kzalloc(fdt_totalsize(fdt1), GFP_KERNEL);
	if (unlikely(!fdt)) {
		ret = -ENOMEM;
		dev_warn(&php_slot->pdev->dev, "Cannot %d bytes memory\n",
			 fdt_totalsize(fdt1));
		goto free_fdt1;
	}

	/* Unflatten device tree blob */
	memcpy(fdt, fdt1, fdt_totalsize(fdt1));
	dt = of_fdt_unflatten_tree(fdt, php_slot->dn, NULL);
	if (unlikely(!dt)) {
		ret = -EINVAL;
		dev_warn(&php_slot->pdev->dev, "Cannot unflatten FDT\n");
		goto free_fdt;
	}

	/* Initialize and apply the changeset */
	of_changeset_init(&php_slot->ocs);
	pnv_php_reverse_nodes(php_slot->dn);
	ret = pnv_php_populate_changeset(&php_slot->ocs, php_slot->dn);
	if (unlikely(ret)) {
		pnv_php_reverse_nodes(php_slot->dn);
		dev_warn(&php_slot->pdev->dev, "Error %d populating changeset\n",
			 ret);
		goto free_dt;
	}

	php_slot->dn->child = NULL;
	ret = of_changeset_apply(&php_slot->ocs);
	if (unlikely(ret)) {
		dev_warn(&php_slot->pdev->dev, "Error %d applying changeset\n",
			 ret);
		goto destroy_changeset;
	}

	/* Add device node firmware data */
	pnv_php_add_pdns(php_slot);
	php_slot->fdt = fdt;
	php_slot->dt  = dt;
	kfree(fdt1);
	goto out;

destroy_changeset:
	of_changeset_destroy(&php_slot->ocs);
free_dt:
	kfree(dt);
	php_slot->dn->child = NULL;
free_fdt:
	kfree(fdt);
free_fdt1:
	kfree(fdt1);
out:
	return ret;
}

int pnv_php_set_slot_power_state(struct hotplug_slot *slot,
				 uint8_t state)
{
	struct pnv_php_slot *php_slot = slot->private;
	struct opal_msg msg;
	int ret;

	ret = pnv_pci_set_power_state(php_slot->id, state, &msg);
	if (likely(ret > 0)) {
		if (be64_to_cpu(msg.params[1]) != php_slot->dn->phandle	||
		    be64_to_cpu(msg.params[2]) != state			||
		    be64_to_cpu(msg.params[3]) != OPAL_SUCCESS) {
			dev_warn(&php_slot->pdev->dev, "Wrong msg (%lld, %lld, %lld)\n",
				 be64_to_cpu(msg.params[1]),
				 be64_to_cpu(msg.params[2]),
				 be64_to_cpu(msg.params[3]));
			return -ENOMSG;
		}
	} else if (unlikely(ret < 0)) {
		dev_warn(&php_slot->pdev->dev, "Error %d powering %s\n",
			 ret, (state == OPAL_PCI_SLOT_POWER_ON) ? "on" : "off");
		return ret;
	}

	if (state == OPAL_PCI_SLOT_POWER_OFF || state == OPAL_PCI_SLOT_OFFLINE)
		pnv_php_rmv_devtree(php_slot);
	else
		ret = pnv_php_add_devtree(php_slot);

	return ret;
}
EXPORT_SYMBOL_GPL(pnv_php_set_slot_power_state);

static int pnv_php_get_power_state(struct hotplug_slot *slot, u8 *state)
{
	struct pnv_php_slot *php_slot = slot->private;
	uint8_t power_state = OPAL_PCI_SLOT_POWER_ON;
	int ret;

	/*
	 * Retrieve power status from firmware. If we fail
	 * getting that, the power status fails back to
	 * be on.
	 */
	ret = pnv_pci_get_power_state(php_slot->id, &power_state);
	if (unlikely(ret)) {
		dev_warn(&php_slot->pdev->dev, "Error %d getting power status\n",
			 ret);
	} else {
		*state = power_state;
		slot->info->power_status = power_state;
	}

	return 0;
}

static int pnv_php_get_adapter_state(struct hotplug_slot *slot, u8 *state)
{
	struct pnv_php_slot *php_slot = slot->private;
	uint8_t presence = OPAL_PCI_SLOT_EMPTY;
	int ret;

	/*
	 * Retrieve presence status from firmware. If we can't
	 * get that, it will fail back to be empty.
	 */
	ret = pnv_pci_get_presence_state(php_slot->id, &presence);
	if (likely(ret >= 0)) {
		*state = presence;
		slot->info->adapter_status = presence;
		ret = 0;
	} else {
		dev_warn(&php_slot->pdev->dev, "Error %d getting presence\n",
			 ret);
	}

	return ret;
}

static int pnv_php_set_attention_state(struct hotplug_slot *slot, u8 state)
{
	/* FIXME: Make it real once firmware supports it */
	slot->info->attention_status = state;

	return 0;
}

static int pnv_php_enable(struct pnv_php_slot *php_slot, bool rescan)
{
	struct hotplug_slot *slot = &php_slot->slot;
	uint8_t presence = OPAL_PCI_SLOT_EMPTY;
	uint8_t power_status = OPAL_PCI_SLOT_POWER_ON;
	int ret;

	/* Check if the slot has been configured */
	if (php_slot->state != PNV_PHP_STATE_REGISTERED)
		return 0;

	/* Retrieve slot presence status */
	ret = pnv_php_get_adapter_state(slot, &presence);
	if (unlikely(ret))
		return ret;

	/* Proceed if there have nothing behind the slot */
	if (presence == OPAL_PCI_SLOT_EMPTY)
		goto scan;

	/*
	 * If the power supply to the slot is off, we can't detect
	 * adapter presence state. That means we have to turn the
	 * slot on before going to probe slot's presence state.
	 *
	 * On the first time, we don't change the power status to
	 * boost system boot with assumption that the firmware
	 * supplies consistent slot power status: empty slot always
	 * has its power off and non-empty slot has its power on.
	 */
	if (!php_slot->power_state_check) {
		php_slot->power_state_check = true;

		ret = pnv_php_get_power_state(slot, &power_status);
		if (unlikely(ret))
			return ret;

		if (power_status != OPAL_PCI_SLOT_POWER_ON)
			return 0;
	}

	/* Check the power status. Scan the slot if it is already on */
	ret = pnv_php_get_power_state(slot, &power_status);
	if (unlikely(ret))
		return ret;

	if (power_status == OPAL_PCI_SLOT_POWER_ON)
		goto scan;

	/* Power is off, turn it on and then scan the slot */
	ret = pnv_php_set_slot_power_state(slot, OPAL_PCI_SLOT_POWER_ON);
	if (unlikely(ret))
		return ret;

scan:
	if (presence == OPAL_PCI_SLOT_PRESENT) {
		if (rescan) {
			pci_lock_rescan_remove();
			pci_hp_add_devices(php_slot->bus);
			pci_unlock_rescan_remove();
		}

		/* Rescan for child hotpluggable slots */
		php_slot->state = PNV_PHP_STATE_POPULATED;
		if (rescan)
			pnv_php_register(php_slot->dn);
	} else {
		php_slot->state = PNV_PHP_STATE_POPULATED;
	}

	return 0;
}

static int pnv_php_enable_slot(struct hotplug_slot *slot)
{
	struct pnv_php_slot *php_slot = container_of(slot,
						     struct pnv_php_slot, slot);

	return pnv_php_enable(php_slot, true);
}

static int pnv_php_disable_slot(struct hotplug_slot *slot)
{
	struct pnv_php_slot *php_slot = slot->private;
	int ret;

	if (php_slot->state != PNV_PHP_STATE_POPULATED)
		return 0;

	/* Remove all devices behind the slot */
	pci_lock_rescan_remove();
	pci_hp_remove_devices(php_slot->bus);
	pci_unlock_rescan_remove();

	/* Detach the child hotpluggable slots */
	pnv_php_unregister(php_slot->dn);

	/* Notify firmware and remove device nodes */
	ret = pnv_php_set_slot_power_state(slot, OPAL_PCI_SLOT_POWER_OFF);

	php_slot->state = PNV_PHP_STATE_REGISTERED;
	return ret;
}

static struct hotplug_slot_ops php_slot_ops = {
	.get_power_status	= pnv_php_get_power_state,
	.get_adapter_status	= pnv_php_get_adapter_state,
	.set_attention_status	= pnv_php_set_attention_state,
	.enable_slot		= pnv_php_enable_slot,
	.disable_slot		= pnv_php_disable_slot,
};

static void pnv_php_release(struct hotplug_slot *slot)
{
	struct pnv_php_slot *php_slot = slot->private;
	unsigned long flags;

	/* Remove from global or child list */
	spin_lock_irqsave(&pnv_php_lock, flags);
	list_del(&php_slot->link);
	spin_unlock_irqrestore(&pnv_php_lock, flags);

	/* Detach from parent */
	pnv_php_put_slot(php_slot);
	pnv_php_put_slot(php_slot->parent);
}

static struct pnv_php_slot *pnv_php_alloc_slot(struct device_node *dn)
{
	struct pnv_php_slot *php_slot;
	struct pci_bus *bus;
	const char *label;
	uint64_t id;

	label = of_get_property(dn, "ibm,slot-label", NULL);
	if (unlikely(!label))
		return NULL;

	if (pnv_pci_get_slot_id(dn, &id))
		return NULL;

	bus = pci_find_bus_by_node(dn);
	if (unlikely(!bus))
		return NULL;

	php_slot = kzalloc(sizeof(*php_slot), GFP_KERNEL);
	if (unlikely(!php_slot))
		return NULL;

	php_slot->name = kstrdup(label, GFP_KERNEL);
	if (unlikely(!php_slot->name)) {
		kfree(php_slot);
		return NULL;
	}

	if (likely(dn->child && PCI_DN(dn->child)))
		php_slot->slot_no = PCI_SLOT(PCI_DN(dn->child)->devfn);
	else
		php_slot->slot_no = -1;   /* Placeholder slot */

	kref_init(&php_slot->kref);
	php_slot->state	                = PNV_PHP_STATE_INITIALIZED;
	php_slot->dn	                = dn;
	php_slot->pdev	                = bus->self;
	php_slot->bus	                = bus;
	php_slot->id	                = id;
	php_slot->power_state_check     = false;
	php_slot->slot.ops              = &php_slot_ops;
	php_slot->slot.info             = &php_slot->slot_info;
	php_slot->slot.release          = pnv_php_release;
	php_slot->slot.private          = php_slot;

	INIT_LIST_HEAD(&php_slot->children);
	INIT_LIST_HEAD(&php_slot->link);

	return php_slot;
}

static int pnv_php_register_slot(struct pnv_php_slot *php_slot)
{
	struct pnv_php_slot *parent;
	struct device_node *dn = php_slot->dn;
	unsigned long flags;
	int ret;

	/* Check if the slot is registered or not */
	parent = pnv_php_find_slot(php_slot->dn);
	if (unlikely(parent)) {
		pnv_php_put_slot(parent);
		return -EEXIST;
	}

	/* Register PCI slot */
	ret = pci_hp_register(&php_slot->slot, php_slot->bus,
			      php_slot->slot_no, php_slot->name);
	if (unlikely(ret)) {
		dev_warn(&php_slot->pdev->dev, "Error %d registering slot\n",
			 ret);
		return ret;
	}

	/* Attach to the parent's child list or global list */
	while ((dn = of_get_parent(dn))) {
		if (!PCI_DN(dn)) {
			of_node_put(dn);
			break;
		}

		parent = pnv_php_find_slot(dn);
		if (parent) {
			of_node_put(dn);
			break;
		}

		of_node_put(dn);
	}

	spin_lock_irqsave(&pnv_php_lock, flags);
	php_slot->parent = parent;
	if (parent)
		list_add_tail(&php_slot->link, &parent->children);
	else
		list_add_tail(&php_slot->link, &pnv_php_slot_list);
	spin_unlock_irqrestore(&pnv_php_lock, flags);

	php_slot->state = PNV_PHP_STATE_REGISTERED;
	return 0;
}

static int pnv_php_register_one(struct device_node *dn)
{
	struct pnv_php_slot *php_slot;
	const __be32 *prop32;
	int ret;

	/* Check if it's hotpluggable slot */
	prop32 = of_get_property(dn, "ibm,slot-pluggable", NULL);
	if (!prop32 || !of_read_number(prop32, 1))
		return -ENXIO;

	prop32 = of_get_property(dn, "ibm,reset-by-firmware", NULL);
	if (!prop32 || !of_read_number(prop32, 1))
		return -ENXIO;

	php_slot = pnv_php_alloc_slot(dn);
	if (unlikely(!php_slot))
		return -ENODEV;

	ret = pnv_php_register_slot(php_slot);
	if (unlikely(ret))
		goto free_slot;

	ret = pnv_php_enable(php_slot, false);
	if (unlikely(ret))
		goto unregister_slot;

	return 0;

unregister_slot:
	pnv_php_unregister_one(php_slot->dn);
free_slot:
	pnv_php_put_slot(php_slot);
	return ret;
}

static void pnv_php_register(struct device_node *dn)
{
	struct device_node *child;

	/*
	 * The parent slots should be registered before their
	 * child slots.
	 */
	for_each_child_of_node(dn, child) {
		pnv_php_register_one(child);
		pnv_php_register(child);
	}
}

static void pnv_php_unregister_one(struct device_node *dn)
{
	struct pnv_php_slot *php_slot;

	php_slot = pnv_php_find_slot(dn);
	if (!php_slot)
		return;

	php_slot->state = PNV_PHP_STATE_OFFLINE;
	pnv_php_put_slot(php_slot);
	pci_hp_deregister(&php_slot->slot);
}

static void pnv_php_unregister(struct device_node *dn)
{
	struct device_node *child;

	/* The child slots should go before their parent slots */
	for_each_child_of_node(dn, child) {
		pnv_php_unregister(child);
		pnv_php_unregister_one(child);
	}
}

static int __init pnv_php_init(void)
{
	struct device_node *dn;

	pr_info(DRIVER_DESC " version: " DRIVER_VERSION "\n");
	for_each_compatible_node(dn, NULL, "ibm,ioda2-phb")
		pnv_php_register(dn);

	return 0;
}

static void __exit pnv_php_exit(void)
{
	struct device_node *dn;

	for_each_compatible_node(dn, NULL, "ibm,ioda2-phb")
		pnv_php_unregister(dn);
}

module_init(pnv_php_init);
module_exit(pnv_php_exit);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
