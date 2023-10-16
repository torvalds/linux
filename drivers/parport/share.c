/*
 * Parallel-port resource manager code.
 *
 * Authors: David Campbell <campbell@tirian.che.curtin.edu.au>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *          Jose Renau <renau@acm.org>
 *          Philip Blundell <philb@gnu.org>
 *	    Andrea Arcangeli
 *
 * based on work by Grant Guenther <grant@torque.net>
 *          and Philip Blundell
 *
 * Any part of this program may be used in documents licensed under
 * the GNU Free Documentation License, Version 1.1 or any later version
 * published by the Free Software Foundation.
 */

#undef PARPORT_DEBUG_SHARING		/* undef for production */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/threads.h>
#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/kmod.h>
#include <linux/device.h>

#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <asm/irq.h>

#undef PARPORT_PARANOID

#define PARPORT_DEFAULT_TIMESLICE	(HZ/5)

unsigned long parport_default_timeslice = PARPORT_DEFAULT_TIMESLICE;
int parport_default_spintime =  DEFAULT_SPIN_TIME;

static LIST_HEAD(portlist);
static DEFINE_SPINLOCK(parportlist_lock);

/* list of all allocated ports, sorted by ->number */
static LIST_HEAD(all_ports);
static DEFINE_SPINLOCK(full_list_lock);

static LIST_HEAD(drivers);

static DEFINE_MUTEX(registration_lock);

/* What you can do to a port that's gone away.. */
static void dead_write_lines(struct parport *p, unsigned char b){}
static unsigned char dead_read_lines(struct parport *p) { return 0; }
static unsigned char dead_frob_lines(struct parport *p, unsigned char b,
			     unsigned char c) { return 0; }
static void dead_onearg(struct parport *p){}
static void dead_initstate(struct pardevice *d, struct parport_state *s) { }
static void dead_state(struct parport *p, struct parport_state *s) { }
static size_t dead_write(struct parport *p, const void *b, size_t l, int f)
{ return 0; }
static size_t dead_read(struct parport *p, void *b, size_t l, int f)
{ return 0; }
static struct parport_operations dead_ops = {
	.write_data	= dead_write_lines,	/* data */
	.read_data	= dead_read_lines,

	.write_control	= dead_write_lines,	/* control */
	.read_control	= dead_read_lines,
	.frob_control	= dead_frob_lines,

	.read_status	= dead_read_lines,	/* status */

	.enable_irq	= dead_onearg,		/* enable_irq */
	.disable_irq	= dead_onearg,		/* disable_irq */

	.data_forward	= dead_onearg,		/* data_forward */
	.data_reverse	= dead_onearg,		/* data_reverse */

	.init_state	= dead_initstate,	/* init_state */
	.save_state	= dead_state,
	.restore_state	= dead_state,

	.epp_write_data	= dead_write,		/* epp */
	.epp_read_data	= dead_read,
	.epp_write_addr	= dead_write,
	.epp_read_addr	= dead_read,

	.ecp_write_data	= dead_write,		/* ecp */
	.ecp_read_data	= dead_read,
	.ecp_write_addr	= dead_write,

	.compat_write_data	= dead_write,	/* compat */
	.nibble_read_data	= dead_read,	/* nibble */
	.byte_read_data		= dead_read,	/* byte */

	.owner		= NULL,
};

static struct device_type parport_device_type = {
	.name = "parport",
};

static int is_parport(struct device *dev)
{
	return dev->type == &parport_device_type;
}

static int parport_probe(struct device *dev)
{
	struct parport_driver *drv;

	if (is_parport(dev))
		return -ENODEV;

	drv = to_parport_driver(dev->driver);
	if (!drv->probe) {
		/* if driver has not defined a custom probe */
		struct pardevice *par_dev = to_pardevice(dev);

		if (strcmp(par_dev->name, drv->name))
			return -ENODEV;
		return 0;
	}
	/* if driver defined its own probe */
	return drv->probe(to_pardevice(dev));
}

static struct bus_type parport_bus_type = {
	.name = "parport",
	.probe = parport_probe,
};

int parport_bus_init(void)
{
	return bus_register(&parport_bus_type);
}

void parport_bus_exit(void)
{
	bus_unregister(&parport_bus_type);
}

/*
 * iterates through all the drivers registered with the bus and sends the port
 * details to the match_port callback of the driver, so that the driver can
 * know about the new port that just registered with the bus and decide if it
 * wants to use this new port.
 */
static int driver_check(struct device_driver *dev_drv, void *_port)
{
	struct parport *port = _port;
	struct parport_driver *drv = to_parport_driver(dev_drv);

	if (drv->match_port)
		drv->match_port(port);
	return 0;
}

/* Call attach(port) for each registered driver. */
static void attach_driver_chain(struct parport *port)
{
	/* caller has exclusive registration_lock */
	struct parport_driver *drv;

	list_for_each_entry(drv, &drivers, list)
		drv->attach(port);

	/*
	 * call the driver_check function of the drivers registered in
	 * new device model
	 */

	bus_for_each_drv(&parport_bus_type, NULL, port, driver_check);
}

static int driver_detach(struct device_driver *_drv, void *_port)
{
	struct parport *port = _port;
	struct parport_driver *drv = to_parport_driver(_drv);

	if (drv->detach)
		drv->detach(port);
	return 0;
}

/* Call detach(port) for each registered driver. */
static void detach_driver_chain(struct parport *port)
{
	struct parport_driver *drv;
	/* caller has exclusive registration_lock */
	list_for_each_entry(drv, &drivers, list)
		drv->detach(port);

	/*
	 * call the detach function of the drivers registered in
	 * new device model
	 */

	bus_for_each_drv(&parport_bus_type, NULL, port, driver_detach);
}

/* Ask kmod for some lowlevel drivers. */
static void get_lowlevel_driver(void)
{
	/*
	 * There is no actual module called this: you should set
	 * up an alias for modutils.
	 */
	request_module("parport_lowlevel");
}

/*
 * iterates through all the devices connected to the bus and sends the device
 * details to the match_port callback of the driver, so that the driver can
 * know what are all the ports that are connected to the bus and choose the
 * port to which it wants to register its device.
 */
static int port_check(struct device *dev, void *dev_drv)
{
	struct parport_driver *drv = dev_drv;

	/* only send ports, do not send other devices connected to bus */
	if (is_parport(dev))
		drv->match_port(to_parport_dev(dev));
	return 0;
}

/*
 * Iterates through all the devices connected to the bus and return 1
 * if the device is a parallel port.
 */

static int port_detect(struct device *dev, void *dev_drv)
{
	if (is_parport(dev))
		return 1;
	return 0;
}

/**
 *	__parport_register_driver - register a parallel port device driver
 *	@drv: structure describing the driver
 *	@owner: owner module of drv
 *	@mod_name: module name string
 *
 *	This can be called by a parallel port device driver in order
 *	to receive notifications about ports being found in the
 *	system, as well as ports no longer available.
 *
 *	If devmodel is true then the new device model is used
 *	for registration.
 *
 *	The @drv structure is allocated by the caller and must not be
 *	deallocated until after calling parport_unregister_driver().
 *
 *	If using the non device model:
 *	The driver's attach() function may block.  The port that
 *	attach() is given will be valid for the duration of the
 *	callback, but if the driver wants to take a copy of the
 *	pointer it must call parport_get_port() to do so.  Calling
 *	parport_register_device() on that port will do this for you.
 *
 *	The driver's detach() function may block.  The port that
 *	detach() is given will be valid for the duration of the
 *	callback, but if the driver wants to take a copy of the
 *	pointer it must call parport_get_port() to do so.
 *
 *
 *	Returns 0 on success. The non device model will always succeeds.
 *	but the new device model can fail and will return the error code.
 **/

int __parport_register_driver(struct parport_driver *drv, struct module *owner,
			      const char *mod_name)
{
	/* using device model */
	int ret;

	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &parport_bus_type;
	drv->driver.owner = owner;
	drv->driver.mod_name = mod_name;
	ret = driver_register(&drv->driver);
	if (ret)
		return ret;

	/*
	 * check if bus has any parallel port registered, if
	 * none is found then load the lowlevel driver.
	 */
	ret = bus_for_each_dev(&parport_bus_type, NULL, NULL,
			       port_detect);
	if (!ret)
		get_lowlevel_driver();

	mutex_lock(&registration_lock);
	if (drv->match_port)
		bus_for_each_dev(&parport_bus_type, NULL, drv,
				 port_check);
	mutex_unlock(&registration_lock);

	return 0;
}
EXPORT_SYMBOL(__parport_register_driver);

static int port_detach(struct device *dev, void *_drv)
{
	struct parport_driver *drv = _drv;

	if (is_parport(dev) && drv->detach)
		drv->detach(to_parport_dev(dev));

	return 0;
}

/**
 *	parport_unregister_driver - deregister a parallel port device driver
 *	@drv: structure describing the driver that was given to
 *	      parport_register_driver()
 *
 *	This should be called by a parallel port device driver that
 *	has registered itself using parport_register_driver() when it
 *	is about to be unloaded.
 *
 *	When it returns, the driver's attach() routine will no longer
 *	be called, and for each port that attach() was called for, the
 *	detach() routine will have been called.
 *
 *	All the driver's attach() and detach() calls are guaranteed to have
 *	finished by the time this function returns.
 **/

void parport_unregister_driver(struct parport_driver *drv)
{
	mutex_lock(&registration_lock);
	bus_for_each_dev(&parport_bus_type, NULL, drv, port_detach);
	driver_unregister(&drv->driver);
	mutex_unlock(&registration_lock);
}
EXPORT_SYMBOL(parport_unregister_driver);

static void free_port(struct device *dev)
{
	int d;
	struct parport *port = to_parport_dev(dev);

	spin_lock(&full_list_lock);
	list_del(&port->full_list);
	spin_unlock(&full_list_lock);
	for (d = 0; d < 5; d++) {
		kfree(port->probe_info[d].class_name);
		kfree(port->probe_info[d].mfr);
		kfree(port->probe_info[d].model);
		kfree(port->probe_info[d].cmdset);
		kfree(port->probe_info[d].description);
	}

	kfree(port->name);
	kfree(port);
}

/**
 *	parport_get_port - increment a port's reference count
 *	@port: the port
 *
 *	This ensures that a struct parport pointer remains valid
 *	until the matching parport_put_port() call.
 **/

struct parport *parport_get_port(struct parport *port)
{
	struct device *dev = get_device(&port->bus_dev);

	return to_parport_dev(dev);
}
EXPORT_SYMBOL(parport_get_port);

void parport_del_port(struct parport *port)
{
	device_unregister(&port->bus_dev);
}
EXPORT_SYMBOL(parport_del_port);

/**
 *	parport_put_port - decrement a port's reference count
 *	@port: the port
 *
 *	This should be called once for each call to parport_get_port(),
 *	once the port is no longer needed. When the reference count reaches
 *	zero (port is no longer used), free_port is called.
 **/

void parport_put_port(struct parport *port)
{
	put_device(&port->bus_dev);
}
EXPORT_SYMBOL(parport_put_port);

/**
 *	parport_register_port - register a parallel port
 *	@base: base I/O address
 *	@irq: IRQ line
 *	@dma: DMA channel
 *	@ops: pointer to the port driver's port operations structure
 *
 *	When a parallel port (lowlevel) driver finds a port that
 *	should be made available to parallel port device drivers, it
 *	should call parport_register_port().  The @base, @irq, and
 *	@dma parameters are for the convenience of port drivers, and
 *	for ports where they aren't meaningful needn't be set to
 *	anything special.  They can be altered afterwards by adjusting
 *	the relevant members of the parport structure that is returned
 *	and represents the port.  They should not be tampered with
 *	after calling parport_announce_port, however.
 *
 *	If there are parallel port device drivers in the system that
 *	have registered themselves using parport_register_driver(),
 *	they are not told about the port at this time; that is done by
 *	parport_announce_port().
 *
 *	The @ops structure is allocated by the caller, and must not be
 *	deallocated before calling parport_remove_port().
 *
 *	If there is no memory to allocate a new parport structure,
 *	this function will return %NULL.
 **/

struct parport *parport_register_port(unsigned long base, int irq, int dma,
				      struct parport_operations *ops)
{
	struct list_head *l;
	struct parport *tmp;
	int num;
	int device;
	int ret;

	tmp = kzalloc(sizeof(struct parport), GFP_KERNEL);
	if (!tmp)
		return NULL;

	/* Init our structure */
	tmp->base = base;
	tmp->irq = irq;
	tmp->dma = dma;
	tmp->muxport = tmp->daisy = tmp->muxsel = -1;
	INIT_LIST_HEAD(&tmp->list);
	tmp->ops = ops;
	tmp->physport = tmp;
	rwlock_init(&tmp->cad_lock);
	spin_lock_init(&tmp->waitlist_lock);
	spin_lock_init(&tmp->pardevice_lock);
	tmp->ieee1284.mode = IEEE1284_MODE_COMPAT;
	tmp->ieee1284.phase = IEEE1284_PH_FWD_IDLE;
	sema_init(&tmp->ieee1284.irq, 0);
	tmp->spintime = parport_default_spintime;
	atomic_set(&tmp->ref_count, 1);
	INIT_LIST_HEAD(&tmp->full_list);

	/* Search for the lowest free parport number. */

	spin_lock(&full_list_lock);
	num = 0;
	list_for_each(l, &all_ports) {
		struct parport *p = list_entry(l, struct parport, full_list);

		if (p->number != num++)
			break;
	}
	tmp->portnum = tmp->number = num;
	list_add_tail(&tmp->full_list, l);
	spin_unlock(&full_list_lock);

	/*
	 * Now that the portnum is known finish doing the Init.
	 */
	tmp->name = kasprintf(GFP_KERNEL, "parport%d", tmp->portnum);
	if (!tmp->name) {
		kfree(tmp);
		return NULL;
	}
	dev_set_name(&tmp->bus_dev, tmp->name);
	tmp->bus_dev.bus = &parport_bus_type;
	tmp->bus_dev.release = free_port;
	tmp->bus_dev.type = &parport_device_type;

	for (device = 0; device < 5; device++)
		/* assume the worst */
		tmp->probe_info[device].class = PARPORT_CLASS_LEGACY;

	tmp->waithead = tmp->waittail = NULL;

	ret = device_register(&tmp->bus_dev);
	if (ret) {
		put_device(&tmp->bus_dev);
		return NULL;
	}

	return tmp;
}
EXPORT_SYMBOL(parport_register_port);

/**
 *	parport_announce_port - tell device drivers about a parallel port
 *	@port: parallel port to announce
 *
 *	After a port driver has registered a parallel port with
 *	parport_register_port, and performed any necessary
 *	initialisation or adjustments, it should call
 *	parport_announce_port() in order to notify all device drivers
 *	that have called parport_register_driver().  Their attach()
 *	functions will be called, with @port as the parameter.
 **/

void parport_announce_port(struct parport *port)
{
	int i;

#ifdef CONFIG_PARPORT_1284
	/* Analyse the IEEE1284.3 topology of the port. */
	parport_daisy_init(port);
#endif

	if (!port->dev)
		pr_warn("%s: fix this legacy no-device port driver!\n",
			port->name);

	parport_proc_register(port);
	mutex_lock(&registration_lock);
	spin_lock_irq(&parportlist_lock);
	list_add_tail(&port->list, &portlist);
	for (i = 1; i < 3; i++) {
		struct parport *slave = port->slaves[i-1];
		if (slave)
			list_add_tail(&slave->list, &portlist);
	}
	spin_unlock_irq(&parportlist_lock);

	/* Let drivers know that new port(s) has arrived. */
	attach_driver_chain(port);
	for (i = 1; i < 3; i++) {
		struct parport *slave = port->slaves[i-1];
		if (slave)
			attach_driver_chain(slave);
	}
	mutex_unlock(&registration_lock);
}
EXPORT_SYMBOL(parport_announce_port);

/**
 *	parport_remove_port - deregister a parallel port
 *	@port: parallel port to deregister
 *
 *	When a parallel port driver is forcibly unloaded, or a
 *	parallel port becomes inaccessible, the port driver must call
 *	this function in order to deal with device drivers that still
 *	want to use it.
 *
 *	The parport structure associated with the port has its
 *	operations structure replaced with one containing 'null'
 *	operations that return errors or just don't do anything.
 *
 *	Any drivers that have registered themselves using
 *	parport_register_driver() are notified that the port is no
 *	longer accessible by having their detach() routines called
 *	with @port as the parameter.
 **/

void parport_remove_port(struct parport *port)
{
	int i;

	mutex_lock(&registration_lock);

	/* Spread the word. */
	detach_driver_chain(port);

#ifdef CONFIG_PARPORT_1284
	/* Forget the IEEE1284.3 topology of the port. */
	parport_daisy_fini(port);
	for (i = 1; i < 3; i++) {
		struct parport *slave = port->slaves[i-1];
		if (!slave)
			continue;
		detach_driver_chain(slave);
		parport_daisy_fini(slave);
	}
#endif

	port->ops = &dead_ops;
	spin_lock(&parportlist_lock);
	list_del_init(&port->list);
	for (i = 1; i < 3; i++) {
		struct parport *slave = port->slaves[i-1];
		if (slave)
			list_del_init(&slave->list);
	}
	spin_unlock(&parportlist_lock);

	mutex_unlock(&registration_lock);

	parport_proc_unregister(port);

	for (i = 1; i < 3; i++) {
		struct parport *slave = port->slaves[i-1];
		if (slave)
			parport_put_port(slave);
	}
}
EXPORT_SYMBOL(parport_remove_port);

static void free_pardevice(struct device *dev)
{
	struct pardevice *par_dev = to_pardevice(dev);

	kfree(par_dev->name);
	kfree(par_dev);
}

/**
 *	parport_register_dev_model - register a device on a parallel port
 *	@port: port to which the device is attached
 *	@name: a name to refer to the device
 *	@par_dev_cb: struct containing callbacks
 *	@id: device number to be given to the device
 *
 *	This function, called by parallel port device drivers,
 *	declares that a device is connected to a port, and tells the
 *	system all it needs to know.
 *
 *	The struct pardev_cb contains pointer to callbacks. preemption
 *	callback function, @preempt, is called when this device driver
 *	has claimed access to the port but another device driver wants
 *	to use it.  It is given, @private, as its parameter, and should
 *	return zero if it is willing for the system to release the port
 *	to another driver on its behalf. If it wants to keep control of
 *	the port it should return non-zero, and no action will be taken.
 *	It is good manners for the driver to try to release the port at
 *	the earliest opportunity after its preemption callback rejects a
 *	preemption attempt. Note that if a preemption callback is happy
 *	for preemption to go ahead, there is no need to release the
 *	port; it is done automatically. This function may not block, as
 *	it may be called from interrupt context. If the device driver
 *	does not support preemption, @preempt can be %NULL.
 *
 *	The wake-up ("kick") callback function, @wakeup, is called when
 *	the port is available to be claimed for exclusive access; that
 *	is, parport_claim() is guaranteed to succeed when called from
 *	inside the wake-up callback function.  If the driver wants to
 *	claim the port it should do so; otherwise, it need not take
 *	any action.  This function may not block, as it may be called
 *	from interrupt context.  If the device driver does not want to
 *	be explicitly invited to claim the port in this way, @wakeup can
 *	be %NULL.
 *
 *	The interrupt handler, @irq_func, is called when an interrupt
 *	arrives from the parallel port.  Note that if a device driver
 *	wants to use interrupts it should use parport_enable_irq(),
 *	and can also check the irq member of the parport structure
 *	representing the port.
 *
 *	The parallel port (lowlevel) driver is the one that has called
 *	request_irq() and whose interrupt handler is called first.
 *	This handler does whatever needs to be done to the hardware to
 *	acknowledge the interrupt (for PC-style ports there is nothing
 *	special to be done).  It then tells the IEEE 1284 code about
 *	the interrupt, which may involve reacting to an IEEE 1284
 *	event depending on the current IEEE 1284 phase.  After this,
 *	it calls @irq_func.  Needless to say, @irq_func will be called
 *	from interrupt context, and may not block.
 *
 *	The %PARPORT_DEV_EXCL flag is for preventing port sharing, and
 *	so should only be used when sharing the port with other device
 *	drivers is impossible and would lead to incorrect behaviour.
 *	Use it sparingly!  Normally, @flags will be zero.
 *
 *	This function returns a pointer to a structure that represents
 *	the device on the port, or %NULL if there is not enough memory
 *	to allocate space for that structure.
 **/

struct pardevice *
parport_register_dev_model(struct parport *port, const char *name,
			   const struct pardev_cb *par_dev_cb, int id)
{
	struct pardevice *par_dev;
	int ret;
	char *devname;

	if (port->physport->flags & PARPORT_FLAG_EXCL) {
		/* An exclusive device is registered. */
		pr_err("%s: no more devices allowed\n", port->name);
		return NULL;
	}

	if (par_dev_cb->flags & PARPORT_DEV_LURK) {
		if (!par_dev_cb->preempt || !par_dev_cb->wakeup) {
			pr_info("%s: refused to register lurking device (%s) without callbacks\n",
				port->name, name);
			return NULL;
		}
	}

	if (par_dev_cb->flags & PARPORT_DEV_EXCL) {
		if (port->physport->devices) {
			/*
			 * If a device is already registered and this new
			 * device wants exclusive access, then no need to
			 * continue as we can not grant exclusive access to
			 * this device.
			 */
			pr_err("%s: cannot grant exclusive access for device %s\n",
			       port->name, name);
			return NULL;
		}
	}

	if (!try_module_get(port->ops->owner))
		return NULL;

	parport_get_port(port);

	par_dev = kzalloc(sizeof(*par_dev), GFP_KERNEL);
	if (!par_dev)
		goto err_put_port;

	par_dev->state = kzalloc(sizeof(*par_dev->state), GFP_KERNEL);
	if (!par_dev->state)
		goto err_put_par_dev;

	devname = kstrdup(name, GFP_KERNEL);
	if (!devname)
		goto err_free_par_dev;

	par_dev->name = devname;
	par_dev->port = port;
	par_dev->daisy = -1;
	par_dev->preempt = par_dev_cb->preempt;
	par_dev->wakeup = par_dev_cb->wakeup;
	par_dev->private = par_dev_cb->private;
	par_dev->flags = par_dev_cb->flags;
	par_dev->irq_func = par_dev_cb->irq_func;
	par_dev->waiting = 0;
	par_dev->timeout = 5 * HZ;

	par_dev->dev.parent = &port->bus_dev;
	par_dev->dev.bus = &parport_bus_type;
	ret = dev_set_name(&par_dev->dev, "%s.%d", devname, id);
	if (ret)
		goto err_free_devname;
	par_dev->dev.release = free_pardevice;
	par_dev->devmodel = true;
	ret = device_register(&par_dev->dev);
	if (ret) {
		kfree(par_dev->state);
		put_device(&par_dev->dev);
		goto err_put_port;
	}

	/* Chain this onto the list */
	par_dev->prev = NULL;
	/*
	 * This function must not run from an irq handler so we don' t need
	 * to clear irq on the local CPU. -arca
	 */
	spin_lock(&port->physport->pardevice_lock);

	if (par_dev_cb->flags & PARPORT_DEV_EXCL) {
		if (port->physport->devices) {
			spin_unlock(&port->physport->pardevice_lock);
			pr_debug("%s: cannot grant exclusive access for device %s\n",
				 port->name, name);
			kfree(par_dev->state);
			device_unregister(&par_dev->dev);
			goto err_put_port;
		}
		port->flags |= PARPORT_FLAG_EXCL;
	}

	par_dev->next = port->physport->devices;
	wmb();	/*
		 * Make sure that tmp->next is written before it's
		 * added to the list; see comments marked 'no locking
		 * required'
		 */
	if (port->physport->devices)
		port->physport->devices->prev = par_dev;
	port->physport->devices = par_dev;
	spin_unlock(&port->physport->pardevice_lock);

	init_waitqueue_head(&par_dev->wait_q);
	par_dev->timeslice = parport_default_timeslice;
	par_dev->waitnext = NULL;
	par_dev->waitprev = NULL;

	/*
	 * This has to be run as last thing since init_state may need other
	 * pardevice fields. -arca
	 */
	port->ops->init_state(par_dev, par_dev->state);
	if (!test_and_set_bit(PARPORT_DEVPROC_REGISTERED, &port->devflags)) {
		port->proc_device = par_dev;
		parport_device_proc_register(par_dev);
	}

	return par_dev;

err_free_devname:
	kfree(devname);
err_free_par_dev:
	kfree(par_dev->state);
err_put_par_dev:
	if (!par_dev->devmodel)
		kfree(par_dev);
err_put_port:
	parport_put_port(port);
	module_put(port->ops->owner);

	return NULL;
}
EXPORT_SYMBOL(parport_register_dev_model);

/**
 *	parport_unregister_device - deregister a device on a parallel port
 *	@dev: pointer to structure representing device
 *
 *	This undoes the effect of parport_register_device().
 **/

void parport_unregister_device(struct pardevice *dev)
{
	struct parport *port;

#ifdef PARPORT_PARANOID
	if (!dev) {
		pr_err("%s: passed NULL\n", __func__);
		return;
	}
#endif

	port = dev->port->physport;

	if (port->proc_device == dev) {
		port->proc_device = NULL;
		clear_bit(PARPORT_DEVPROC_REGISTERED, &port->devflags);
		parport_device_proc_unregister(dev);
	}

	if (port->cad == dev) {
		printk(KERN_DEBUG "%s: %s forgot to release port\n",
		       port->name, dev->name);
		parport_release(dev);
	}

	spin_lock(&port->pardevice_lock);
	if (dev->next)
		dev->next->prev = dev->prev;
	if (dev->prev)
		dev->prev->next = dev->next;
	else
		port->devices = dev->next;

	if (dev->flags & PARPORT_DEV_EXCL)
		port->flags &= ~PARPORT_FLAG_EXCL;

	spin_unlock(&port->pardevice_lock);

	/*
	 * Make sure we haven't left any pointers around in the wait
	 * list.
	 */
	spin_lock_irq(&port->waitlist_lock);
	if (dev->waitprev || dev->waitnext || port->waithead == dev) {
		if (dev->waitprev)
			dev->waitprev->waitnext = dev->waitnext;
		else
			port->waithead = dev->waitnext;
		if (dev->waitnext)
			dev->waitnext->waitprev = dev->waitprev;
		else
			port->waittail = dev->waitprev;
	}
	spin_unlock_irq(&port->waitlist_lock);

	kfree(dev->state);
	device_unregister(&dev->dev);

	module_put(port->ops->owner);
	parport_put_port(port);
}
EXPORT_SYMBOL(parport_unregister_device);

/**
 *	parport_find_number - find a parallel port by number
 *	@number: parallel port number
 *
 *	This returns the parallel port with the specified number, or
 *	%NULL if there is none.
 *
 *	There is an implicit parport_get_port() done already; to throw
 *	away the reference to the port that parport_find_number()
 *	gives you, use parport_put_port().
 */

struct parport *parport_find_number(int number)
{
	struct parport *port, *result = NULL;

	if (list_empty(&portlist))
		get_lowlevel_driver();

	spin_lock(&parportlist_lock);
	list_for_each_entry(port, &portlist, list) {
		if (port->number == number) {
			result = parport_get_port(port);
			break;
		}
	}
	spin_unlock(&parportlist_lock);
	return result;
}
EXPORT_SYMBOL(parport_find_number);

/**
 *	parport_find_base - find a parallel port by base address
 *	@base: base I/O address
 *
 *	This returns the parallel port with the specified base
 *	address, or %NULL if there is none.
 *
 *	There is an implicit parport_get_port() done already; to throw
 *	away the reference to the port that parport_find_base()
 *	gives you, use parport_put_port().
 */

struct parport *parport_find_base(unsigned long base)
{
	struct parport *port, *result = NULL;

	if (list_empty(&portlist))
		get_lowlevel_driver();

	spin_lock(&parportlist_lock);
	list_for_each_entry(port, &portlist, list) {
		if (port->base == base) {
			result = parport_get_port(port);
			break;
		}
	}
	spin_unlock(&parportlist_lock);
	return result;
}
EXPORT_SYMBOL(parport_find_base);

/**
 *	parport_claim - claim access to a parallel port device
 *	@dev: pointer to structure representing a device on the port
 *
 *	This function will not block and so can be used from interrupt
 *	context.  If parport_claim() succeeds in claiming access to
 *	the port it returns zero and the port is available to use.  It
 *	may fail (returning non-zero) if the port is in use by another
 *	driver and that driver is not willing to relinquish control of
 *	the port.
 **/

int parport_claim(struct pardevice *dev)
{
	struct pardevice *oldcad;
	struct parport *port = dev->port->physport;
	unsigned long flags;

	if (port->cad == dev) {
		pr_info("%s: %s already owner\n", dev->port->name, dev->name);
		return 0;
	}

	/* Preempt any current device */
	write_lock_irqsave(&port->cad_lock, flags);
	oldcad = port->cad;
	if (oldcad) {
		if (oldcad->preempt) {
			if (oldcad->preempt(oldcad->private))
				goto blocked;
			port->ops->save_state(port, dev->state);
		} else
			goto blocked;

		if (port->cad != oldcad) {
			/*
			 * I think we'll actually deadlock rather than
			 * get here, but just in case..
			 */
			pr_warn("%s: %s released port when preempted!\n",
				port->name, oldcad->name);
			if (port->cad)
				goto blocked;
		}
	}

	/* Can't fail from now on, so mark ourselves as no longer waiting.  */
	if (dev->waiting & 1) {
		dev->waiting = 0;

		/* Take ourselves out of the wait list again.  */
		spin_lock_irq(&port->waitlist_lock);
		if (dev->waitprev)
			dev->waitprev->waitnext = dev->waitnext;
		else
			port->waithead = dev->waitnext;
		if (dev->waitnext)
			dev->waitnext->waitprev = dev->waitprev;
		else
			port->waittail = dev->waitprev;
		spin_unlock_irq(&port->waitlist_lock);
		dev->waitprev = dev->waitnext = NULL;
	}

	/* Now we do the change of devices */
	port->cad = dev;

#ifdef CONFIG_PARPORT_1284
	/* If it's a mux port, select it. */
	if (dev->port->muxport >= 0) {
		/* FIXME */
		port->muxsel = dev->port->muxport;
	}

	/* If it's a daisy chain device, select it. */
	if (dev->daisy >= 0) {
		/* This could be lazier. */
		if (!parport_daisy_select(port, dev->daisy,
					   IEEE1284_MODE_COMPAT))
			port->daisy = dev->daisy;
	}
#endif /* IEEE1284.3 support */

	/* Restore control registers */
	port->ops->restore_state(port, dev->state);
	write_unlock_irqrestore(&port->cad_lock, flags);
	dev->time = jiffies;
	return 0;

blocked:
	/*
	 * If this is the first time we tried to claim the port, register an
	 * interest.  This is only allowed for devices sleeping in
	 * parport_claim_or_block(), or those with a wakeup function.
	 */

	/* The cad_lock is still held for writing here */
	if (dev->waiting & 2 || dev->wakeup) {
		spin_lock(&port->waitlist_lock);
		if (test_and_set_bit(0, &dev->waiting) == 0) {
			/* First add ourselves to the end of the wait list. */
			dev->waitnext = NULL;
			dev->waitprev = port->waittail;
			if (port->waittail) {
				port->waittail->waitnext = dev;
				port->waittail = dev;
			} else
				port->waithead = port->waittail = dev;
		}
		spin_unlock(&port->waitlist_lock);
	}
	write_unlock_irqrestore(&port->cad_lock, flags);
	return -EAGAIN;
}
EXPORT_SYMBOL(parport_claim);

/**
 *	parport_claim_or_block - claim access to a parallel port device
 *	@dev: pointer to structure representing a device on the port
 *
 *	This behaves like parport_claim(), but will block if necessary
 *	to wait for the port to be free.  A return value of 1
 *	indicates that it slept; 0 means that it succeeded without
 *	needing to sleep.  A negative error code indicates failure.
 **/

int parport_claim_or_block(struct pardevice *dev)
{
	int r;

	/*
	 * Signal to parport_claim() that we can wait even without a
	 * wakeup function.
	 */
	dev->waiting = 2;

	/* Try to claim the port.  If this fails, we need to sleep.  */
	r = parport_claim(dev);
	if (r == -EAGAIN) {
#ifdef PARPORT_DEBUG_SHARING
		printk(KERN_DEBUG "%s: parport_claim() returned -EAGAIN\n",
		       dev->name);
#endif
		/*
		 * FIXME!!! Use the proper locking for dev->waiting,
		 * and make this use the "wait_event_interruptible()"
		 * interfaces. The cli/sti that used to be here
		 * did nothing.
		 *
		 * See also parport_release()
		 */

		/*
		 * If dev->waiting is clear now, an interrupt
		 * gave us the port and we would deadlock if we slept.
		 */
		if (dev->waiting) {
			wait_event_interruptible(dev->wait_q,
						 !dev->waiting);
			if (signal_pending(current))
				return -EINTR;
			r = 1;
		} else {
			r = 0;
#ifdef PARPORT_DEBUG_SHARING
			printk(KERN_DEBUG "%s: didn't sleep in parport_claim_or_block()\n",
			       dev->name);
#endif
		}

#ifdef PARPORT_DEBUG_SHARING
		if (dev->port->physport->cad != dev)
			printk(KERN_DEBUG "%s: exiting parport_claim_or_block but %s owns port!\n",
			       dev->name, dev->port->physport->cad ?
			       dev->port->physport->cad->name : "nobody");
#endif
	}
	dev->waiting = 0;
	return r;
}
EXPORT_SYMBOL(parport_claim_or_block);

/**
 *	parport_release - give up access to a parallel port device
 *	@dev: pointer to structure representing parallel port device
 *
 *	This function cannot fail, but it should not be called without
 *	the port claimed.  Similarly, if the port is already claimed
 *	you should not try claiming it again.
 **/

void parport_release(struct pardevice *dev)
{
	struct parport *port = dev->port->physport;
	struct pardevice *pd;
	unsigned long flags;

	/* Make sure that dev is the current device */
	write_lock_irqsave(&port->cad_lock, flags);
	if (port->cad != dev) {
		write_unlock_irqrestore(&port->cad_lock, flags);
		pr_warn("%s: %s tried to release parport when not owner\n",
			port->name, dev->name);
		return;
	}

#ifdef CONFIG_PARPORT_1284
	/* If this is on a mux port, deselect it. */
	if (dev->port->muxport >= 0) {
		/* FIXME */
		port->muxsel = -1;
	}

	/* If this is a daisy device, deselect it. */
	if (dev->daisy >= 0) {
		parport_daisy_deselect_all(port);
		port->daisy = -1;
	}
#endif

	port->cad = NULL;
	write_unlock_irqrestore(&port->cad_lock, flags);

	/* Save control registers */
	port->ops->save_state(port, dev->state);

	/*
	 * If anybody is waiting, find out who's been there longest and
	 * then wake them up. (Note: no locking required)
	 */
	/* !!! LOCKING IS NEEDED HERE */
	for (pd = port->waithead; pd; pd = pd->waitnext) {
		if (pd->waiting & 2) { /* sleeping in claim_or_block */
			parport_claim(pd);
			if (waitqueue_active(&pd->wait_q))
				wake_up_interruptible(&pd->wait_q);
			return;
		} else if (pd->wakeup) {
			pd->wakeup(pd->private);
			if (dev->port->cad) /* racy but no matter */
				return;
		} else {
			pr_err("%s: don't know how to wake %s\n",
			       port->name, pd->name);
		}
	}

	/*
	 * Nobody was waiting, so walk the list to see if anyone is
	 * interested in being woken up. (Note: no locking required)
	 */
	/* !!! LOCKING IS NEEDED HERE */
	for (pd = port->devices; !port->cad && pd; pd = pd->next) {
		if (pd->wakeup && pd != dev)
			pd->wakeup(pd->private);
	}
}
EXPORT_SYMBOL(parport_release);

irqreturn_t parport_irq_handler(int irq, void *dev_id)
{
	struct parport *port = dev_id;

	parport_generic_irq(port);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(parport_irq_handler);

MODULE_LICENSE("GPL");
