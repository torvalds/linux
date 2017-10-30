/*
 * (c) 2017 Stefano Stabellini <stefano@aporeto.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>

#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/interface/io/pvcalls.h>

#define PVCALLS_INVALID_ID UINT_MAX
#define PVCALLS_RING_ORDER XENBUS_MAX_RING_GRANT_ORDER
#define PVCALLS_NR_RSP_PER_RING __CONST_RING_SIZE(xen_pvcalls, XEN_PAGE_SIZE)

struct pvcalls_bedata {
	struct xen_pvcalls_front_ring ring;
	grant_ref_t ref;
	int irq;

	struct list_head socket_mappings;
	spinlock_t socket_lock;

	wait_queue_head_t inflight_req;
	struct xen_pvcalls_response rsp[PVCALLS_NR_RSP_PER_RING];
};
/* Only one front/back connection supported. */
static struct xenbus_device *pvcalls_front_dev;
static atomic_t pvcalls_refcount;

/* first increment refcount, then proceed */
#define pvcalls_enter() {               \
	atomic_inc(&pvcalls_refcount);      \
}

/* first complete other operations, then decrement refcount */
#define pvcalls_exit() {                \
	atomic_dec(&pvcalls_refcount);      \
}

struct sock_mapping {
	bool active_socket;
	struct list_head list;
	struct socket *sock;
};

static irqreturn_t pvcalls_front_event_handler(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

static void pvcalls_front_free_map(struct pvcalls_bedata *bedata,
				   struct sock_mapping *map)
{
}

static const struct xenbus_device_id pvcalls_front_ids[] = {
	{ "pvcalls" },
	{ "" }
};

static int pvcalls_front_remove(struct xenbus_device *dev)
{
	struct pvcalls_bedata *bedata;
	struct sock_mapping *map = NULL, *n;

	bedata = dev_get_drvdata(&pvcalls_front_dev->dev);
	dev_set_drvdata(&dev->dev, NULL);
	pvcalls_front_dev = NULL;
	if (bedata->irq >= 0)
		unbind_from_irqhandler(bedata->irq, dev);

	smp_mb();
	while (atomic_read(&pvcalls_refcount) > 0)
		cpu_relax();
	list_for_each_entry_safe(map, n, &bedata->socket_mappings, list) {
		if (map->active_socket) {
			/* No need to lock, refcount is 0 */
			pvcalls_front_free_map(bedata, map);
		} else {
			list_del(&map->list);
			kfree(map);
		}
	}
	if (bedata->ref >= 0)
		gnttab_end_foreign_access(bedata->ref, 0, 0);
	kfree(bedata->ring.sring);
	kfree(bedata);
	xenbus_switch_state(dev, XenbusStateClosed);
	return 0;
}

static int pvcalls_front_probe(struct xenbus_device *dev,
			  const struct xenbus_device_id *id)
{
	return 0;
}

static void pvcalls_front_changed(struct xenbus_device *dev,
			    enum xenbus_state backend_state)
{
}

static struct xenbus_driver pvcalls_front_driver = {
	.ids = pvcalls_front_ids,
	.probe = pvcalls_front_probe,
	.remove = pvcalls_front_remove,
	.otherend_changed = pvcalls_front_changed,
};

static int __init pvcalls_frontend_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	pr_info("Initialising Xen pvcalls frontend driver\n");

	return xenbus_register_frontend(&pvcalls_front_driver);
}

module_init(pvcalls_frontend_init);
