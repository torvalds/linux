/*
 * IEEE 1284.3 Parallel port daisy chain and multiplexor code
 * 
 * Copyright (C) 1999, 2000  Tim Waugh <tim@cyberelk.demon.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * ??-12-1998: Initial implementation.
 * 31-01-1999: Make port-cloning transparent.
 * 13-02-1999: Move DeviceID technique from parport_probe.
 * 13-03-1999: Get DeviceID from non-IEEE 1284.3 devices too.
 * 22-02-2000: Count devices that are actually detected.
 *
 * Any part of this program may be used in documents licensed under
 * the GNU Free Documentation License, Version 1.1 or any later version
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>

#include <asm/current.h>
#include <linux/uaccess.h>

#undef DEBUG

#ifdef DEBUG
#define DPRINTK(stuff...) printk(stuff)
#else
#define DPRINTK(stuff...)
#endif

static struct daisydev {
	struct daisydev *next;
	struct parport *port;
	int daisy;
	int devnum;
} *topology = NULL;
static DEFINE_SPINLOCK(topology_lock);

static int numdevs;
static bool daisy_init_done;

/* Forward-declaration of lower-level functions. */
static int mux_present(struct parport *port);
static int num_mux_ports(struct parport *port);
static int select_port(struct parport *port);
static int assign_addrs(struct parport *port);

/* Add a device to the discovered topology. */
static void add_dev(int devnum, struct parport *port, int daisy)
{
	struct daisydev *newdev, **p;
	newdev = kmalloc(sizeof(struct daisydev), GFP_KERNEL);
	if (newdev) {
		newdev->port = port;
		newdev->daisy = daisy;
		newdev->devnum = devnum;
		spin_lock(&topology_lock);
		for (p = &topology; *p && (*p)->devnum<devnum; p = &(*p)->next)
			;
		newdev->next = *p;
		*p = newdev;
		spin_unlock(&topology_lock);
	}
}

/* Clone a parport (actually, make an alias). */
static struct parport *clone_parport(struct parport *real, int muxport)
{
	struct parport *extra = parport_register_port(real->base,
						       real->irq,
						       real->dma,
						       real->ops);
	if (extra) {
		extra->portnum = real->portnum;
		extra->physport = real;
		extra->muxport = muxport;
		real->slaves[muxport-1] = extra;
	}

	return extra;
}

static int daisy_drv_probe(struct pardevice *par_dev)
{
	struct device_driver *drv = par_dev->dev.driver;

	if (strcmp(drv->name, "daisy_drv"))
		return -ENODEV;
	if (strcmp(par_dev->name, daisy_dev_name))
		return -ENODEV;

	return 0;
}

static struct parport_driver daisy_driver = {
	.name = "daisy_drv",
	.probe = daisy_drv_probe,
	.devmodel = true,
};

/* Discover the IEEE1284.3 topology on a port -- muxes and daisy chains.
 * Return value is number of devices actually detected. */
int parport_daisy_init(struct parport *port)
{
	int detected = 0;
	char *deviceid;
	static const char *th[] = { /*0*/"th", "st", "nd", "rd", "th" };
	int num_ports;
	int i;
	int last_try = 0;

	if (!daisy_init_done) {
		/*
		 * flag should be marked true first as
		 * parport_register_driver() might try to load the low
		 * level driver which will lead to announcing new ports
		 * and which will again come back here at
		 * parport_daisy_init()
		 */
		daisy_init_done = true;
		i = parport_register_driver(&daisy_driver);
		if (i) {
			pr_err("daisy registration failed\n");
			daisy_init_done = false;
			return i;
		}
	}

again:
	/* Because this is called before any other devices exist,
	 * we don't have to claim exclusive access.  */

	/* If mux present on normal port, need to create new
	 * parports for each extra port. */
	if (port->muxport < 0 && mux_present(port) &&
	    /* don't be fooled: a mux must have 2 or 4 ports. */
	    ((num_ports = num_mux_ports(port)) == 2 || num_ports == 4)) {
		/* Leave original as port zero. */
		port->muxport = 0;
		printk(KERN_INFO
			"%s: 1st (default) port of %d-way multiplexor\n",
			port->name, num_ports);
		for (i = 1; i < num_ports; i++) {
			/* Clone the port. */
			struct parport *extra = clone_parport(port, i);
			if (!extra) {
				if (signal_pending(current))
					break;

				schedule();
				continue;
			}

			printk(KERN_INFO
				"%s: %d%s port of %d-way multiplexor on %s\n",
				extra->name, i + 1, th[i + 1], num_ports,
				port->name);

			/* Analyse that port too.  We won't recurse
			   forever because of the 'port->muxport < 0'
			   test above. */
			parport_daisy_init(extra);
		}
	}

	if (port->muxport >= 0)
		select_port(port);

	parport_daisy_deselect_all(port);
	detected += assign_addrs(port);

	/* Count the potential legacy device at the end. */
	add_dev(numdevs++, port, -1);

	/* Find out the legacy device's IEEE 1284 device ID. */
	deviceid = kmalloc(1024, GFP_KERNEL);
	if (deviceid) {
		if (parport_device_id(numdevs - 1, deviceid, 1024) > 2)
			detected++;

		kfree(deviceid);
	}

	if (!detected && !last_try) {
		/* No devices were detected.  Perhaps they are in some
                   funny state; let's try to reset them and see if
                   they wake up. */
		parport_daisy_fini(port);
		parport_write_control(port, PARPORT_CONTROL_SELECT);
		udelay(50);
		parport_write_control(port,
				       PARPORT_CONTROL_SELECT |
				       PARPORT_CONTROL_INIT);
		udelay(50);
		last_try = 1;
		goto again;
	}

	return detected;
}

/* Forget about devices on a physical port. */
void parport_daisy_fini(struct parport *port)
{
	struct daisydev **p;

	spin_lock(&topology_lock);
	p = &topology;
	while (*p) {
		struct daisydev *dev = *p;
		if (dev->port != port) {
			p = &dev->next;
			continue;
		}
		*p = dev->next;
		kfree(dev);
	}

	/* Gaps in the numbering could be handled better.  How should
           someone enumerate through all IEEE1284.3 devices in the
           topology?. */
	if (!topology) numdevs = 0;
	spin_unlock(&topology_lock);
	return;
}

/**
 *	parport_open - find a device by canonical device number
 *	@devnum: canonical device number
 *	@name: name to associate with the device
 *
 *	This function is similar to parport_register_device(), except
 *	that it locates a device by its number rather than by the port
 *	it is attached to.
 *
 *	All parameters except for @devnum are the same as for
 *	parport_register_device().  The return value is the same as
 *	for parport_register_device().
 **/

struct pardevice *parport_open(int devnum, const char *name)
{
	struct daisydev *p = topology;
	struct pardev_cb par_cb;
	struct parport *port;
	struct pardevice *dev;
	int daisy;

	memset(&par_cb, 0, sizeof(par_cb));
	spin_lock(&topology_lock);
	while (p && p->devnum != devnum)
		p = p->next;

	if (!p) {
		spin_unlock(&topology_lock);
		return NULL;
	}

	daisy = p->daisy;
	port = parport_get_port(p->port);
	spin_unlock(&topology_lock);

	dev = parport_register_dev_model(port, name, &par_cb, devnum);
	parport_put_port(port);
	if (!dev)
		return NULL;

	dev->daisy = daisy;

	/* Check that there really is a device to select. */
	if (daisy >= 0) {
		int selected;
		parport_claim_or_block(dev);
		selected = port->daisy;
		parport_release(dev);

		if (selected != daisy) {
			/* No corresponding device. */
			parport_unregister_device(dev);
			return NULL;
		}
	}

	return dev;
}

/**
 *	parport_close - close a device opened with parport_open()
 *	@dev: device to close
 *
 *	This is to parport_open() as parport_unregister_device() is to
 *	parport_register_device().
 **/

void parport_close(struct pardevice *dev)
{
	parport_unregister_device(dev);
}

/* Send a daisy-chain-style CPP command packet. */
static int cpp_daisy(struct parport *port, int cmd)
{
	unsigned char s;

	parport_data_forward(port);
	parport_write_data(port, 0xaa); udelay(2);
	parport_write_data(port, 0x55); udelay(2);
	parport_write_data(port, 0x00); udelay(2);
	parport_write_data(port, 0xff); udelay(2);
	s = parport_read_status(port) & (PARPORT_STATUS_BUSY
					  | PARPORT_STATUS_PAPEROUT
					  | PARPORT_STATUS_SELECT
					  | PARPORT_STATUS_ERROR);
	if (s != (PARPORT_STATUS_BUSY
		  | PARPORT_STATUS_PAPEROUT
		  | PARPORT_STATUS_SELECT
		  | PARPORT_STATUS_ERROR)) {
		DPRINTK(KERN_DEBUG "%s: cpp_daisy: aa5500ff(%02x)\n",
			 port->name, s);
		return -ENXIO;
	}

	parport_write_data(port, 0x87); udelay(2);
	s = parport_read_status(port) & (PARPORT_STATUS_BUSY
					  | PARPORT_STATUS_PAPEROUT
					  | PARPORT_STATUS_SELECT
					  | PARPORT_STATUS_ERROR);
	if (s != (PARPORT_STATUS_SELECT | PARPORT_STATUS_ERROR)) {
		DPRINTK(KERN_DEBUG "%s: cpp_daisy: aa5500ff87(%02x)\n",
			 port->name, s);
		return -ENXIO;
	}

	parport_write_data(port, 0x78); udelay(2);
	parport_write_data(port, cmd); udelay(2);
	parport_frob_control(port,
			      PARPORT_CONTROL_STROBE,
			      PARPORT_CONTROL_STROBE);
	udelay(1);
	s = parport_read_status(port);
	parport_frob_control(port, PARPORT_CONTROL_STROBE, 0);
	udelay(1);
	parport_write_data(port, 0xff); udelay(2);

	return s;
}

/* Send a mux-style CPP command packet. */
static int cpp_mux(struct parport *port, int cmd)
{
	unsigned char s;
	int rc;

	parport_data_forward(port);
	parport_write_data(port, 0xaa); udelay(2);
	parport_write_data(port, 0x55); udelay(2);
	parport_write_data(port, 0xf0); udelay(2);
	parport_write_data(port, 0x0f); udelay(2);
	parport_write_data(port, 0x52); udelay(2);
	parport_write_data(port, 0xad); udelay(2);
	parport_write_data(port, cmd); udelay(2);

	s = parport_read_status(port);
	if (!(s & PARPORT_STATUS_ACK)) {
		DPRINTK(KERN_DEBUG "%s: cpp_mux: aa55f00f52ad%02x(%02x)\n",
			 port->name, cmd, s);
		return -EIO;
	}

	rc = (((s & PARPORT_STATUS_SELECT   ? 1 : 0) << 0) |
	      ((s & PARPORT_STATUS_PAPEROUT ? 1 : 0) << 1) |
	      ((s & PARPORT_STATUS_BUSY     ? 0 : 1) << 2) |
	      ((s & PARPORT_STATUS_ERROR    ? 0 : 1) << 3));

	return rc;
}

void parport_daisy_deselect_all(struct parport *port)
{
	cpp_daisy(port, 0x30);
}

int parport_daisy_select(struct parport *port, int daisy, int mode)
{
	switch (mode)
	{
		// For these modes we should switch to EPP mode:
		case IEEE1284_MODE_EPP:
		case IEEE1284_MODE_EPPSL:
		case IEEE1284_MODE_EPPSWE:
			return !(cpp_daisy(port, 0x20 + daisy) &
				 PARPORT_STATUS_ERROR);

		// For these modes we should switch to ECP mode:
		case IEEE1284_MODE_ECP:
		case IEEE1284_MODE_ECPRLE:
		case IEEE1284_MODE_ECPSWE: 
			return !(cpp_daisy(port, 0xd0 + daisy) &
				 PARPORT_STATUS_ERROR);

		// Nothing was told for BECP in Daisy chain specification.
		// May be it's wise to use ECP?
		case IEEE1284_MODE_BECP:
		// Others use compat mode
		case IEEE1284_MODE_NIBBLE:
		case IEEE1284_MODE_BYTE:
		case IEEE1284_MODE_COMPAT:
		default:
			return !(cpp_daisy(port, 0xe0 + daisy) &
				 PARPORT_STATUS_ERROR);
	}
}

static int mux_present(struct parport *port)
{
	return cpp_mux(port, 0x51) == 3;
}

static int num_mux_ports(struct parport *port)
{
	return cpp_mux(port, 0x58);
}

static int select_port(struct parport *port)
{
	int muxport = port->muxport;
	return cpp_mux(port, 0x60 + muxport) == muxport;
}

static int assign_addrs(struct parport *port)
{
	unsigned char s;
	unsigned char daisy;
	int thisdev = numdevs;
	int detected;
	char *deviceid;

	parport_data_forward(port);
	parport_write_data(port, 0xaa); udelay(2);
	parport_write_data(port, 0x55); udelay(2);
	parport_write_data(port, 0x00); udelay(2);
	parport_write_data(port, 0xff); udelay(2);
	s = parport_read_status(port) & (PARPORT_STATUS_BUSY
					  | PARPORT_STATUS_PAPEROUT
					  | PARPORT_STATUS_SELECT
					  | PARPORT_STATUS_ERROR);
	if (s != (PARPORT_STATUS_BUSY
		  | PARPORT_STATUS_PAPEROUT
		  | PARPORT_STATUS_SELECT
		  | PARPORT_STATUS_ERROR)) {
		DPRINTK(KERN_DEBUG "%s: assign_addrs: aa5500ff(%02x)\n",
			 port->name, s);
		return 0;
	}

	parport_write_data(port, 0x87); udelay(2);
	s = parport_read_status(port) & (PARPORT_STATUS_BUSY
					  | PARPORT_STATUS_PAPEROUT
					  | PARPORT_STATUS_SELECT
					  | PARPORT_STATUS_ERROR);
	if (s != (PARPORT_STATUS_SELECT | PARPORT_STATUS_ERROR)) {
		DPRINTK(KERN_DEBUG "%s: assign_addrs: aa5500ff87(%02x)\n",
			 port->name, s);
		return 0;
	}

	parport_write_data(port, 0x78); udelay(2);
	s = parport_read_status(port);

	for (daisy = 0;
	     (s & (PARPORT_STATUS_PAPEROUT|PARPORT_STATUS_SELECT))
		     == (PARPORT_STATUS_PAPEROUT|PARPORT_STATUS_SELECT)
		     && daisy < 4;
	     ++daisy) {
		parport_write_data(port, daisy);
		udelay(2);
		parport_frob_control(port,
				      PARPORT_CONTROL_STROBE,
				      PARPORT_CONTROL_STROBE);
		udelay(1);
		parport_frob_control(port, PARPORT_CONTROL_STROBE, 0);
		udelay(1);

		add_dev(numdevs++, port, daisy);

		/* See if this device thought it was the last in the
		 * chain. */
		if (!(s & PARPORT_STATUS_BUSY))
			break;

		/* We are seeing pass through status now. We see
		   last_dev from next device or if last_dev does not
		   work status lines from some non-daisy chain
		   device. */
		s = parport_read_status(port);
	}

	parport_write_data(port, 0xff); udelay(2);
	detected = numdevs - thisdev;
	DPRINTK(KERN_DEBUG "%s: Found %d daisy-chained devices\n", port->name,
		 detected);

	/* Ask the new devices to introduce themselves. */
	deviceid = kmalloc(1024, GFP_KERNEL);
	if (!deviceid) return 0;

	for (daisy = 0; thisdev < numdevs; thisdev++, daisy++)
		parport_device_id(thisdev, deviceid, 1024);

	kfree(deviceid);
	return detected;
}
