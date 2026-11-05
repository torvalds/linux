// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 NXP
 * Generic I3C Hub core implementing virtual controller operations.
 */
#include <linux/i3c/device.h>
#include <linux/i3c/hub.h>
#include <linux/lockdep.h>

#include "internals.h"

/**
 * struct i3c_hub_dev_data - Per-downstream-device hub state
 * @parent_desc: Permanent parent-facing descriptor whose master points at the
 *		 physical parent controller, used to forward controller-specific
 *		 operations there.
 *
 * The logical descriptor on the virtual hub bus keeps its master aimed at the
 * virtual hub controller and is never modified. This separate descriptor lets
 * the controller-only core helpers resolve the physical parent without racing
 * concurrent readers on the virtual bus.
 */
struct i3c_hub_dev_data {
	struct i3c_dev_desc parent_desc;
};

/*
 * All i3c_bus rw_semaphores are initialized from a single call site in the
 * I3C core, so lockdep assigns them one shared class. When a hub forwards an
 * operation it takes the parent bus lock while already holding its own virtual
 * bus lock, which lockdep then reports as recursive locking on that shared
 * class. The bus maintenance and normal-use helpers use plain down_write() and
 * down_read(), which always acquire with subclass 0, so lockdep_set_subclass()
 * cannot separate them; a distinct lock_class_key per nesting level is used
 * instead.
 *
 * A top-level hub uses depth 1, a hub behind another hub uses depth 2, and so
 * on, so a virtual bus lock never shares a class with the parent bus lock it
 * nests under. Sibling ports on the same hub share a class, which is safe
 * because they are never nested against each other. The array must stay a
 * file-local definition: lockdep keys are identified by their address, so a
 * single set of unique objects is required.
 *
 * The depth bound is generous; exceeding it only loses lockdep coverage, not
 * correctness.
 */
#define I3C_HUB_MAX_LOCK_DEPTH 8
static struct lock_class_key i3c_hub_bus_lock_keys[I3C_HUB_MAX_LOCK_DEPTH];

/*
 * The hub routing mutex (hub->lock) serializes port switching and forwarding.
 * A child hub holds its routing mutex while reaching a parent hub that takes
 * its own, so it needs the same per-depth lock_class_key treatment as the bus
 * lock above, keyed identically (top-level hub depth 1, and so on). The class
 * is assigned once in i3c_hub_init(), not per port, because all ports on a hub
 * share this single routing mutex.
 */
static struct lock_class_key i3c_hub_routing_lock_keys[I3C_HUB_MAX_LOCK_DEPTH];

/**
 * i3c_hub_controller_depth() - Count hub nesting levels above a controller
 * @controller: Virtual hub controller being initialized
 *
 * Walk the parent chain and count how many stacked hub controllers lead to
 * @controller. A top-level hub attached to a physical controller returns 1.
 * The walk stops at the first non-hub (physical) controller.
 *
 * Return: The hub nesting depth (>= 1 for a hub controller).
 */
static unsigned int
i3c_hub_controller_depth(struct i3c_master_controller *controller)
{
	struct i3c_hub_controller *hub_controller;
	unsigned int depth = 0;

	while (controller && controller->ops == &i3c_hub_master_ops) {
		hub_controller = dev_get_drvdata(&controller->dev);
		if (!hub_controller)
			break;
		controller = hub_controller->parent;
		depth++;
	}

	return depth;
}

/**
 * i3c_hub_master_bus_init() - Bind controller to hub device
 * @controller: Virtual controller for a hub port
 *
 * Associates the virtual controller with the hub device descriptor so that
 * transfers are executed through the hub on the parent bus.
 */
static int i3c_hub_master_bus_init(struct i3c_master_controller *controller)
{
	struct i3c_hub_controller *hub_controller;
	struct i3c_hub *hub;
	unsigned int depth;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return -ENODEV;

	hub = hub_controller->hub;

	if (!hub->hub_dev)
		return -ENODEV;

	/*
	 * Give this virtual bus lock a lockdep class keyed on its hub nesting
	 * depth before the core runs the first DAA (which forwards to the
	 * parent bus and takes the parent lock while this one is held). The
	 * lock is not held here, and controller->ops is already set, so the
	 * class can be assigned safely. Deeper hubs than the key array
	 * supports fall back to the shared class and may warn under lockdep,
	 * but still function correctly.
	 */
	depth = i3c_hub_controller_depth(controller);
	if (depth >= 1 && depth <= I3C_HUB_MAX_LOCK_DEPTH)
		lockdep_set_class(&controller->bus.lock,
				  &i3c_hub_bus_lock_keys[depth - 1]);
	else
		WARN_ONCE(1, "i3c-hub: nesting depth %u exceeds lockdep support\n",
			  depth);

	controller->this = hub->hub_dev->desc;
	return 0;
}

static void i3c_hub_master_bus_cleanup(struct i3c_master_controller *controller)
{
	controller->this = NULL;
}

static int i3c_hub_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *controller = i3c_dev_get_master(dev);
	struct i3c_hub_controller *hub_controller;
	struct i3c_hub_dev_data *data;
	struct i3c_master_controller *parent;
	struct i3c_hub *hub;
	int ret;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return -ENODEV;

	hub = hub_controller->hub;
	if (!hub->hub_dev)
		return -ENODEV;

	parent = i3c_dev_get_master(hub->hub_dev->desc);
	if (!parent)
		return -ENODEV;

	data = kzalloc_obj(*data);
	if (!data)
		return -ENOMEM;

	/* Initialize the parent-facing descriptor to target the physical parent. */
	INIT_LIST_HEAD(&data->parent_desc.common.node);
	mutex_init(&data->parent_desc.ibi_lock);

	data->parent_desc.common.master = parent;
	data->parent_desc.info = dev->info;

	i3c_bus_maintenance_lock(&parent->bus);
	ret = i3c_master_attach_i3c_dev_controller_locked(&data->parent_desc);
	i3c_bus_maintenance_unlock(&parent->bus);
	if (ret) {
		mutex_destroy(&data->parent_desc.ibi_lock);
		kfree(data);
		return ret;
	}

	/* Link the hub-private data (see struct i3c_hub_dev_data). */
	i3c_dev_set_master_data(dev, data);

	return 0;
}

static int i3c_hub_reattach_i3c_dev(struct i3c_dev_desc *dev,
				    u8 old_dyn_addr)
{
	struct i3c_hub_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *parent;
	int ret;

	if (!data)
		return -ENODEV;

	parent = i3c_dev_get_master(&data->parent_desc);
	if (!parent)
		return -ENODEV;

	/*
	 * Reattach must not race asynchronous IBI delivery on the parent-facing
	 * descriptor. Once IBI resources are requested the parent controller may
	 * use parent_desc concurrently, so reject a reattach that arrives while
	 * the generic IBI object is still live.
	 */
	if (WARN_ON_ONCE(data->parent_desc.ibi))
		return -EBUSY;

	/*
	 * Re-sync device information after the address change and reattach
	 * under the parent bus lock so both updates are applied as one
	 * operation with respect to parent controller state.
	 */
	i3c_bus_maintenance_lock(&parent->bus);
	data->parent_desc.info = dev->info;
	ret = i3c_master_reattach_i3c_dev_controller_locked(&data->parent_desc,
							    old_dyn_addr);
	i3c_bus_maintenance_unlock(&parent->bus);

	return ret;
}

static void i3c_hub_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_hub_dev_data *data = i3c_dev_get_master_data(dev);
	struct i3c_master_controller *parent;

	if (!data)
		return;

	parent = i3c_dev_get_master(&data->parent_desc);

	/*
	 * parent_desc.ibi should already be cleared by i3c_hub_free_ibi()
	 * before we get here. If it is still set, the kfree(data) below frees
	 * a descriptor the parent controller can still reach via un-flushed
	 * asynchronous IBI work (use-after-free, not just a leak).
	 */
	WARN_ON_ONCE(data->parent_desc.ibi);

	if (parent) {
		i3c_bus_maintenance_lock(&parent->bus);
		i3c_master_detach_i3c_dev_controller_locked(&data->parent_desc);
		i3c_bus_maintenance_unlock(&parent->bus);
	}

	i3c_dev_set_master_data(dev, NULL);
	mutex_destroy(&data->parent_desc.ibi_lock);
	kfree(data);
}

/**
 * i3c_hub_do_daa() - Perform DAA via hub port
 * @hub: Hub instance
 * @controller: Virtual controller for a hub port
 *
 * Enables the port connection, performs DAA on the parent controller,
 * then disables the connection.
 */
static int i3c_hub_do_daa(struct i3c_hub *hub,
			  struct i3c_master_controller *controller)
{
	struct i3c_master_controller *parent;
	int ret;

	if (!hub || !hub->hub_dev)
		return -ENODEV;

	parent = i3c_dev_get_master(hub->hub_dev->desc);
	if (!parent)
		return -ENODEV;

	/*
	 * Lock order: hub routing mutex before the parent bus lock (taken here
	 * inside i3c_master_do_daa()). The depth-keyed lockdep classes above
	 * keep this nesting acyclic when the parent is itself a hub.
	 */
	mutex_lock(&hub->lock);
	i3c_hub_enable_port(controller);

	/*
	 * Downstream devices reachable through hub target-port routes share the
	 * parent controller's I3C address space. The hub gates access to a
	 * target-port network, but it does not create an independent dynamic
	 * address domain per virtual bus.
	 *
	 * Run DAA on the parent controller so dynamic addresses remain unique
	 * across all downstream devices, even when they are behind different
	 * target ports.
	 *
	 * The current hub model shares the parent controller's dynamic-address
	 * domain and does not support independent per-port DAA domains, so a
	 * DAA result does not go stale behind a port toggle. Addresses reserved
	 * from the Device Tree are held for the lifetime of the parent bus and
	 * are never released on a port toggle, so enabling or disabling a port
	 * only gates reachability and never causes an address to be handed out
	 * twice.
	 */
	ret = i3c_master_do_daa(parent);
	i3c_hub_disable_port(controller);
	mutex_unlock(&hub->lock);

	return ret;
}

static bool i3c_hub_supports_ccc_cmd(struct i3c_hub *hub,
				     const struct i3c_ccc_cmd *cmd)
{
	struct i3c_master_controller *parent;

	if (!hub || !hub->hub_dev)
		return false;

	parent = i3c_dev_get_master(hub->hub_dev->desc);
	if (!parent)
		return false;

	return i3c_master_supports_ccc_cmd(parent, cmd);
}

/**
 * i3c_hub_send_ccc_cmd() - Send CCC through hub port
 * @hub: Hub instance
 * @controller: Virtual controller
 * @cmd: CCC command
 *
 * Enables the port connection while issuing CCC on the parent controller.
 *
 * A hub does not isolate the CCC address space of its virtual buses from the
 * parent bus. A broadcast CCC issued on one virtual bus is forwarded to the
 * parent controller and therefore reaches every device on the parent bus, as
 * well as the downstream devices of any other connected target port. Drivers
 * that need per-device scope must use the directed form of the command.
 */
static int i3c_hub_send_ccc_cmd(struct i3c_hub *hub,
				struct i3c_master_controller *controller,
				struct i3c_ccc_cmd *cmd)
{
	struct i3c_master_controller *parent;
	int ret;

	if (!hub || !hub->hub_dev)
		return -ENODEV;

	parent = i3c_dev_get_master(hub->hub_dev->desc);
	if (!parent)
		return -ENODEV;

	mutex_lock(&hub->lock);
	i3c_hub_enable_port(controller);
	ret = i3c_master_send_ccc_cmd(parent, cmd);
	i3c_hub_disable_port(controller);
	mutex_unlock(&hub->lock);

	return ret;
}

/**
 * i3c_hub_master_priv_xfers() - Execute private transfers via hub
 * @dev: Target device descriptor
 * @xfers: Transfer array
 * @nxfers: Number of transfers
 * @mode: Transfer mode (SDR, HDR, etc.)
 *
 * Refreshes the parent-facing device info (while no IBI is pending) and
 * forwards private transfers through the hub to the parent controller.
 */
static int i3c_hub_master_priv_xfers(struct i3c_dev_desc *dev,
				     struct i3c_xfer *xfers,
				     int nxfers,
				     enum i3c_xfer_mode mode)
{
	struct i3c_master_controller *controller = i3c_dev_get_master(dev);
	struct i3c_hub_controller *hub_controller;
	struct i3c_master_controller *parent;
	struct i3c_hub_dev_data *data;
	struct i3c_hub *hub;
	int ret;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return -ENODEV;

	hub = hub_controller->hub;

	data = i3c_dev_get_master_data(dev);
	if (!data)
		return -ENODEV;

	parent = i3c_dev_get_master(&data->parent_desc);
	if (!parent)
		return -ENODEV;

	/* Lock order: hub routing mutex before the parent bus lock (see do_daa). */
	mutex_lock(&hub->lock);

	/*
	 * Only refresh the parent-facing info while no IBI is requested; once
	 * parent_desc.ibi is set it must stay immutable (see i3c_hub_request_ibi()).
	 */
	if (!data->parent_desc.ibi)
		data->parent_desc.info = dev->info;

	i3c_hub_enable_port(controller);

	i3c_bus_normaluse_lock(&parent->bus);
	ret = i3c_dev_do_xfers_locked(&data->parent_desc, xfers,
				      nxfers, mode);
	i3c_bus_normaluse_unlock(&parent->bus);

	i3c_hub_disable_port(controller);

	mutex_unlock(&hub->lock);

	return ret;
}

static int i3c_hub_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	return -EOPNOTSUPP;
}

static void i3c_hub_detach_i2c_dev(struct i2c_dev_desc *dev)
{
}

static int i3c_hub_i2c_xfers(struct i2c_dev_desc *dev,
			     struct i2c_msg *xfers, int nxfers)
{
	return -EOPNOTSUPP;
}

static int i3c_hub_master_do_daa(struct i3c_master_controller *controller)
{
	struct i3c_hub_controller *hub_controller;
	struct i3c_hub *hub;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return -ENODEV;

	hub = hub_controller->hub;

	return i3c_hub_do_daa(hub, controller);
}

static int i3c_hub_master_send_ccc_cmd(struct i3c_master_controller *controller,
				       struct i3c_ccc_cmd *cmd)
{
	struct i3c_hub_controller *hub_controller;
	struct i3c_hub *hub;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return -ENODEV;

	hub = hub_controller->hub;

	if (!hub->hub_dev)
		return -ENODEV;

	/*
	 * Do not forward broadcast RSTDAA through the hub. The hub itself
	 * is visible on the parent bus, so forwarding RSTDAA would also
	 * reset the hub dynamic address. Downstream RSTDAA is not supported
	 * by the hub virtual-controller model.
	 */
	if (cmd->id == I3C_CCC_RSTDAA(true))
		return 0;

	return i3c_hub_send_ccc_cmd(hub, controller, cmd);
}

static bool i3c_hub_master_supports_ccc_cmd(struct i3c_master_controller *controller,
					    const struct i3c_ccc_cmd *cmd)
{
	struct i3c_hub_controller *hub_controller;
	struct i3c_hub *hub;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return false;

	hub = hub_controller->hub;

	return i3c_hub_supports_ccc_cmd(hub, cmd);
}

/**
 * i3c_hub_request_ibi() - Request IBI through parent controller
 * @desc: Target device descriptor
 * @req: IBI setup
 *
 * Publishes the generic IBI object on the permanent parent-facing descriptor
 * and requests IBI for a device connected through the hub. The parent-facing
 * descriptor references the same IBI object so the physical controller uses
 * the logical workqueue, pending counter and client device during
 * asynchronous IBI delivery.
 */
static int i3c_hub_request_ibi(struct i3c_dev_desc *desc,
			       const struct i3c_ibi_setup *req)
{
	struct i3c_master_controller *controller = i3c_dev_get_master(desc);
	struct i3c_hub_controller *hub_controller;
	struct i3c_master_controller *parent;
	struct i3c_hub_dev_data *data;
	struct i3c_hub *hub;
	int ret;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return -ENODEV;

	hub = hub_controller->hub;

	data = i3c_dev_get_master_data(desc);
	if (!data)
		return -ENODEV;

	parent = i3c_dev_get_master(&data->parent_desc);
	if (!parent)
		return -ENODEV;

	/*
	 * Publish the final device information snapshot together with the
	 * generic IBI object under hub->lock. Keep the parent-facing
	 * information immutable while parent_desc.ibi is set and the parent
	 * controller may use the descriptor asynchronously.
	 */
	mutex_lock(&hub->lock);
	data->parent_desc.info = desc->info;
	data->parent_desc.dev = desc->dev;
	data->parent_desc.ibi = desc->ibi;
	mutex_unlock(&hub->lock);

	i3c_bus_normaluse_lock(&parent->bus);
	ret = i3c_dev_request_ibi_controller_locked(&data->parent_desc, req);
	i3c_bus_normaluse_unlock(&parent->bus);

	if (ret) {
		mutex_lock(&hub->lock);
		data->parent_desc.ibi = NULL;
		data->parent_desc.dev = NULL;
		mutex_unlock(&hub->lock);
	}

	return ret;
}

static void i3c_hub_free_ibi(struct i3c_dev_desc *desc)
{
	struct i3c_master_controller *controller = i3c_dev_get_master(desc);
	struct i3c_hub_controller *hub_controller;
	struct i3c_master_controller *parent;
	struct i3c_hub_dev_data *data;
	struct i3c_hub *hub;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return;

	hub = hub_controller->hub;

	data = i3c_dev_get_master_data(desc);
	if (!data || !data->parent_desc.ibi)
		return;

	parent = i3c_dev_get_master(&data->parent_desc);
	if (!parent)
		return;

	i3c_bus_normaluse_lock(&parent->bus);
	i3c_dev_free_ibi_controller_locked(&data->parent_desc);
	i3c_bus_normaluse_unlock(&parent->bus);

	/*
	 * The outer generic IBI free path owns and releases desc->ibi after
	 * this callback returns.
	 */
	mutex_lock(&hub->lock);
	data->parent_desc.ibi = NULL;
	data->parent_desc.dev = NULL;
	mutex_unlock(&hub->lock);
}

/**
 * i3c_hub_enable_ibi() - Enable IBI via hub port
 * @desc: Target device descriptor
 *
 * Enables port connection and forwards the IBI enable request to the parent
 * controller.
 */
static int i3c_hub_enable_ibi(struct i3c_dev_desc *desc)
{
	struct i3c_master_controller *controller = i3c_dev_get_master(desc);
	struct i3c_hub_controller *hub_controller;
	struct i3c_master_controller *parent;
	struct i3c_hub_dev_data *data;
	struct i3c_hub *hub;
	int ret;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return -ENODEV;

	hub = hub_controller->hub;

	data = i3c_dev_get_master_data(desc);
	if (!data || !data->parent_desc.ibi)
		return -ENODEV;

	parent = i3c_dev_get_master(&data->parent_desc);
	if (!parent)
		return -ENODEV;

	mutex_lock(&hub->lock);

	i3c_hub_enable_port(controller);

	i3c_bus_maintenance_lock(&parent->bus);
	ret = i3c_dev_enable_ibi_controller_locked(&data->parent_desc);
	i3c_bus_maintenance_unlock(&parent->bus);

	i3c_hub_disable_port(controller);

	mutex_unlock(&hub->lock);

	return ret;
}

/**
 * i3c_hub_disable_ibi() - Disable IBI via hub port
 * @desc: Target device descriptor
 *
 * Enables port connection and forwards the IBI disable request to the parent
 * controller.
 */
static int i3c_hub_disable_ibi(struct i3c_dev_desc *desc)
{
	struct i3c_master_controller *controller = i3c_dev_get_master(desc);
	struct i3c_hub_controller *hub_controller;
	struct i3c_master_controller *parent;
	struct i3c_hub_dev_data *data;
	struct i3c_hub *hub;
	int ret;

	hub_controller = dev_get_drvdata(&controller->dev);
	if (!hub_controller || !hub_controller->hub)
		return -ENODEV;

	hub = hub_controller->hub;

	data = i3c_dev_get_master_data(desc);
	if (!data || !data->parent_desc.ibi)
		return -ENODEV;

	parent = i3c_dev_get_master(&data->parent_desc);
	if (!parent)
		return -ENODEV;

	mutex_lock(&hub->lock);

	i3c_hub_enable_port(controller);

	i3c_bus_maintenance_lock(&parent->bus);
	ret = i3c_dev_disable_ibi_controller_locked(&data->parent_desc);
	i3c_bus_maintenance_unlock(&parent->bus);

	i3c_hub_disable_port(controller);

	mutex_unlock(&hub->lock);

	return ret;
}

static void i3c_hub_recycle_ibi_slot(struct i3c_dev_desc *desc,
				     struct i3c_ibi_slot *slot)
{
	struct i3c_hub_dev_data *data = i3c_dev_get_master_data(desc);

	if (!data)
		return;

	i3c_dev_recycle_ibi_slot_controller(&data->parent_desc, slot);
}

const struct i3c_master_controller_ops i3c_hub_master_ops = {
	.bus_init = i3c_hub_master_bus_init,
	.bus_cleanup = i3c_hub_master_bus_cleanup,
	.attach_i3c_dev = i3c_hub_attach_i3c_dev,
	.reattach_i3c_dev = i3c_hub_reattach_i3c_dev,
	.detach_i3c_dev = i3c_hub_detach_i3c_dev,
	.do_daa = i3c_hub_master_do_daa,
	.supports_ccc_cmd = i3c_hub_master_supports_ccc_cmd,
	.send_ccc_cmd = i3c_hub_master_send_ccc_cmd,
	.i3c_xfers = i3c_hub_master_priv_xfers,
	.attach_i2c_dev = i3c_hub_attach_i2c_dev,
	.detach_i2c_dev = i3c_hub_detach_i2c_dev,
	.i2c_xfers = i3c_hub_i2c_xfers,
	.request_ibi = i3c_hub_request_ibi,
	.free_ibi = i3c_hub_free_ibi,
	.enable_ibi = i3c_hub_enable_ibi,
	.disable_ibi = i3c_hub_disable_ibi,
	.recycle_ibi_slot = i3c_hub_recycle_ibi_slot,
};
EXPORT_SYMBOL_GPL(i3c_hub_master_ops);

/**
 * i3c_hub_set_routing_lock_class() - Key the routing mutex for lockdep
 * @hub: Hub instance whose routing mutex has just been initialized
 *
 * Give @hub->lock a lockdep class keyed on the hub nesting depth, so that a
 * child hub taking its routing mutex while a parent hub holds its own is not
 * reported as recursive locking. Does nothing when lockdep is disabled.
 */
static void i3c_hub_set_routing_lock_class(struct i3c_hub *hub)
{
	struct i3c_master_controller *parent;
	unsigned int depth;

	if (!IS_ENABLED(CONFIG_LOCKDEP))
		return;

	/*
	 * The routing mutex has the same hub nesting depth as the virtual
	 * controllers this hub exposes, so the parent controller is one level
	 * shallower. Keying it once here, rather than per port, avoids
	 * reclassifying the single shared routing mutex from a later port that
	 * may already have used it.
	 */
	parent = i3c_dev_get_master(hub->hub_dev->desc);
	depth = i3c_hub_controller_depth(parent) + 1;
	if (WARN_ONCE(depth > I3C_HUB_MAX_LOCK_DEPTH,
		      "i3c-hub: routing lock depth %u exceeds lockdep support\n",
		      depth))
		depth = I3C_HUB_MAX_LOCK_DEPTH;

	lockdep_set_class(&hub->lock, &i3c_hub_routing_lock_keys[depth - 1]);
}

/**
 * i3c_hub_init() - Initialize hub context
 * @hub: Hub instance to initialize
 * @ops: Vendor callbacks used to connect and disconnect target ports
 * @hub_dev: I3C device representing the hub on the parent bus
 *
 * Store the vendor callbacks and the hub device in @hub and initialize the
 * routing mutex used to serialize port switching and forwarding.
 *
 * @hub_dev must be a registered I3C device, so it already has a descriptor
 * and a parent controller.
 */
void i3c_hub_init(struct i3c_hub *hub,
		  const struct i3c_hub_ops *ops,
		  struct i3c_device *hub_dev)
{
	hub->ops = ops;
	hub->hub_dev = hub_dev;
	mutex_init(&hub->lock);

	i3c_hub_set_routing_lock_class(hub);
}
EXPORT_SYMBOL_GPL(i3c_hub_init);

/**
 * i3c_hub_reserve_parent_addrslots_from_dt() - Reserve child addresses in parent bus.
 * @hubc: I3C hub controller for a target-port virtual bus.
 * @node: Target-port bus Device Tree node.
 *
 * Reserve parent bus address slots for the assigned dynamic address of each
 * downstream I3C device described in the Device Tree, so that parent DAA does
 * not hand the same address to another device. Downstream devices behind hub
 * target ports share the parent controller's dynamic address space.
 *
 * The reservations describe a fixed Device Tree topology and are kept for the
 * lifetime of the parent bus; they are not released when a target port is
 * disconnected. If the hub is removed and probed again it re-reads the same
 * Device Tree and reserves the same addresses, so repeated probing does not
 * consume additional address space.
 *
 * Only the assigned dynamic addresses are reserved here. A hub that supports
 * hot-joining downstream devices would additionally have to reserve the static
 * addresses of every device that may appear. Supporting runtime hub hotplug
 * would also require tracking ownership of these reservations and releasing
 * them when a hub is removed.
 *
 * Return: 0 on success, or a negative error code.
 */
int i3c_hub_reserve_parent_addrslots_from_dt(struct i3c_hub_controller *hubc,
					     struct device_node *node)
{
	struct i3c_master_controller *parent = hubc->parent;
	enum i3c_addr_slot_status status;
	u32 assigned_addr;
	u32 reg[3];
	int ret;

	if (!parent || !node)
		return -ENODEV;

	for_each_available_child_of_node_scoped(node, child) {
		/*
		 * Only consider addressable bus nodes: a valid "reg" is
		 * required to describe a device, but its static-address value
		 * does not affect the reservation below.
		 */
		ret = of_property_read_variable_u32_array(child, "reg", reg, 1, 3);
		if (ret < 0)
			continue;

		ret = of_property_read_u32(child, "assigned-address", &assigned_addr);
		if (ret)
			continue;

		/* Skip nodes without a usable dynamic address. */
		if (!assigned_addr || assigned_addr > I3C_MAX_ADDR)
			continue;

		i3c_bus_maintenance_lock(&parent->bus);
		status = i3c_bus_get_addr_slot_status(&parent->bus,
						      assigned_addr);
		if (status == I3C_ADDR_SLOT_FREE)
			i3c_bus_set_addr_slot_status(&parent->bus,
						     assigned_addr,
						     I3C_ADDR_SLOT_I3C_DEV);
		i3c_bus_maintenance_unlock(&parent->bus);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(i3c_hub_reserve_parent_addrslots_from_dt);

MODULE_AUTHOR("Aman Kumar Pandey <aman.kumarpandey@nxp.com>");
MODULE_AUTHOR("Vikash Bansal <vikash.bansal@nxp.com>");
MODULE_AUTHOR("Lakshay Piplani <lakshay.piplani@nxp.com>");
MODULE_DESCRIPTION("Generic I3C hub support");
MODULE_LICENSE("GPL");
