// SPDX-License-Identifier: GPL-2.0
/* vcc.c: sun4v virtual channel concentrator
 *
 * Copyright (C) 2017 Oracle. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/termios_internal.h>
#include <asm/vio.h>
#include <asm/ldc.h>

MODULE_DESCRIPTION("Sun LDOM virtual console concentrator driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");

struct vcc_port {
	struct vio_driver_state vio;

	spinlock_t lock;
	char *domain;
	struct tty_struct *tty;	/* only populated while dev is open */
	unsigned long index;	/* index into the vcc_table */

	u64 refcnt;
	bool excl_locked;

	bool removed;

	/* This buffer is required to support the tty write_room interface
	 * and guarantee that any characters that the driver accepts will
	 * be eventually sent, either immediately or later.
	 */
	size_t chars_in_buffer;
	struct vio_vcc buffer;

	struct timer_list rx_timer;
	struct timer_list tx_timer;
};

/* Microseconds that thread will delay waiting for a vcc port ref */
#define VCC_REF_DELAY		100

#define VCC_MAX_PORTS		1024
#define VCC_MINOR_START		0	/* must be zero */
#define VCC_BUFF_LEN		VIO_VCC_MTU_SIZE

#define VCC_CTL_BREAK		-1
#define VCC_CTL_HUP		-2

static struct tty_driver *vcc_tty_driver;

static struct vcc_port *vcc_table[VCC_MAX_PORTS];
static DEFINE_SPINLOCK(vcc_table_lock);

static unsigned int vcc_dbg;
static unsigned int vcc_dbg_ldc;
static unsigned int vcc_dbg_vio;

module_param(vcc_dbg, uint, 0664);
module_param(vcc_dbg_ldc, uint, 0664);
module_param(vcc_dbg_vio, uint, 0664);

#define VCC_DBG_DRV	0x1
#define VCC_DBG_LDC	0x2
#define VCC_DBG_PKT	0x4

#define vccdbg(f, a...)						\
	do {							\
		if (vcc_dbg & VCC_DBG_DRV)			\
			pr_info(f, ## a);			\
	} while (0)						\

#define vccdbgl(l)						\
	do {							\
		if (vcc_dbg & VCC_DBG_LDC)			\
			ldc_print(l);				\
	} while (0)						\

#define vccdbgp(pkt)						\
	do {							\
		if (vcc_dbg & VCC_DBG_PKT) {			\
			int i;					\
			for (i = 0; i < pkt.tag.stype; i++)	\
				pr_info("[%c]", pkt.data[i]);	\
		}						\
	} while (0)						\

/* Note: Be careful when adding flags to this line discipline.  Don't
 * add anything that will cause echoing or we'll go into recursive
 * loop echoing chars back and forth with the console drivers.
 */
static const struct ktermios vcc_tty_termios = {
	.c_iflag = IGNBRK | IGNPAR,
	.c_oflag = OPOST,
	.c_cflag = B38400 | CS8 | CREAD | HUPCL,
	.c_cc = INIT_C_CC,
	.c_ispeed = 38400,
	.c_ospeed = 38400
};

/**
 * vcc_table_add() - Add VCC port to the VCC table
 * @port: pointer to the VCC port
 *
 * Return: index of the port in the VCC table on success,
 *	   -1 on failure
 */
static int vcc_table_add(struct vcc_port *port)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&vcc_table_lock, flags);
	for (i = VCC_MINOR_START; i < VCC_MAX_PORTS; i++) {
		if (!vcc_table[i]) {
			vcc_table[i] = port;
			break;
		}
	}
	spin_unlock_irqrestore(&vcc_table_lock, flags);

	if (i < VCC_MAX_PORTS)
		return i;
	else
		return -1;
}

/**
 * vcc_table_remove() - Removes a VCC port from the VCC table
 * @index: Index into the VCC table
 */
static void vcc_table_remove(unsigned long index)
{
	unsigned long flags;

	if (WARN_ON(index >= VCC_MAX_PORTS))
		return;

	spin_lock_irqsave(&vcc_table_lock, flags);
	vcc_table[index] = NULL;
	spin_unlock_irqrestore(&vcc_table_lock, flags);
}

/**
 * vcc_get() - Gets a reference to VCC port
 * @index: Index into the VCC table
 * @excl: Indicates if an exclusive access is requested
 *
 * Return: reference to the VCC port, if found
 *	   NULL, if port not found
 */
static struct vcc_port *vcc_get(unsigned long index, bool excl)
{
	struct vcc_port *port;
	unsigned long flags;

try_again:
	spin_lock_irqsave(&vcc_table_lock, flags);

	port = vcc_table[index];
	if (!port) {
		spin_unlock_irqrestore(&vcc_table_lock, flags);
		return NULL;
	}

	if (!excl) {
		if (port->excl_locked) {
			spin_unlock_irqrestore(&vcc_table_lock, flags);
			udelay(VCC_REF_DELAY);
			goto try_again;
		}
		port->refcnt++;
		spin_unlock_irqrestore(&vcc_table_lock, flags);
		return port;
	}

	if (port->refcnt) {
		spin_unlock_irqrestore(&vcc_table_lock, flags);
		/* Threads wanting exclusive access will wait half the time,
		 * probably giving them higher priority in the case of
		 * multiple waiters.
		 */
		udelay(VCC_REF_DELAY/2);
		goto try_again;
	}

	port->refcnt++;
	port->excl_locked = true;
	spin_unlock_irqrestore(&vcc_table_lock, flags);

	return port;
}

/**
 * vcc_put() - Returns a reference to VCC port
 * @port: pointer to VCC port
 * @excl: Indicates if the returned reference is an exclusive reference
 *
 * Note: It's the caller's responsibility to ensure the correct value
 *	 for the excl flag
 */
static void vcc_put(struct vcc_port *port, bool excl)
{
	unsigned long flags;

	if (!port)
		return;

	spin_lock_irqsave(&vcc_table_lock, flags);

	/* check if caller attempted to put with the wrong flags */
	if (WARN_ON((excl && !port->excl_locked) ||
		    (!excl && port->excl_locked)))
		goto done;

	port->refcnt--;

	if (excl)
		port->excl_locked = false;

done:
	spin_unlock_irqrestore(&vcc_table_lock, flags);
}

/**
 * vcc_get_ne() - Get a non-exclusive reference to VCC port
 * @index: Index into the VCC table
 *
 * Gets a non-exclusive reference to VCC port, if it's not removed
 *
 * Return: pointer to the VCC port, if found
 *	   NULL, if port not found
 */
static struct vcc_port *vcc_get_ne(unsigned long index)
{
	struct vcc_port *port;

	port = vcc_get(index, false);

	if (port && port->removed) {
		vcc_put(port, false);
		return NULL;
	}

	return port;
}

static void vcc_kick_rx(struct vcc_port *port)
{
	struct vio_driver_state *vio = &port->vio;

	assert_spin_locked(&port->lock);

	if (!timer_pending(&port->rx_timer) && !port->removed) {
		disable_irq_nosync(vio->vdev->rx_irq);
		port->rx_timer.expires = (jiffies + 1);
		add_timer(&port->rx_timer);
	}
}

static void vcc_kick_tx(struct vcc_port *port)
{
	assert_spin_locked(&port->lock);

	if (!timer_pending(&port->tx_timer) && !port->removed) {
		port->tx_timer.expires = (jiffies + 1);
		add_timer(&port->tx_timer);
	}
}

static int vcc_rx_check(struct tty_struct *tty, int size)
{
	if (WARN_ON(!tty || !tty->port))
		return 1;

	/* tty_buffer_request_room won't sleep because it uses
	 * GFP_ATOMIC flag to allocate buffer
	 */
	if (test_bit(TTY_THROTTLED, &tty->flags) ||
	    (tty_buffer_request_room(tty->port, VCC_BUFF_LEN) < VCC_BUFF_LEN))
		return 0;

	return 1;
}

static int vcc_rx(struct tty_struct *tty, char *buf, int size)
{
	int len = 0;

	if (WARN_ON(!tty || !tty->port))
		return len;

	len = tty_insert_flip_string(tty->port, buf, size);
	if (len)
		tty_flip_buffer_push(tty->port);

	return len;
}

static int vcc_ldc_read(struct vcc_port *port)
{
	struct vio_driver_state *vio = &port->vio;
	struct tty_struct *tty;
	struct vio_vcc pkt;
	int rv = 0;

	tty = port->tty;
	if (!tty) {
		rv = ldc_rx_reset(vio->lp);
		vccdbg("VCC: reset rx q: rv=%d\n", rv);
		goto done;
	}

	/* Read as long as LDC has incoming data. */
	while (1) {
		if (!vcc_rx_check(tty, VIO_VCC_MTU_SIZE)) {
			vcc_kick_rx(port);
			break;
		}

		vccdbgl(vio->lp);

		rv = ldc_read(vio->lp, &pkt, sizeof(pkt));
		if (rv <= 0)
			break;

		vccdbg("VCC: ldc_read()=%d\n", rv);
		vccdbg("TAG [%02x:%02x:%04x:%08x]\n",
		       pkt.tag.type, pkt.tag.stype,
		       pkt.tag.stype_env, pkt.tag.sid);

		if (pkt.tag.type == VIO_TYPE_DATA) {
			vccdbgp(pkt);
			/* vcc_rx_check ensures memory availability */
			vcc_rx(tty, pkt.data, pkt.tag.stype);
		} else {
			pr_err("VCC: unknown msg [%02x:%02x:%04x:%08x]\n",
			       pkt.tag.type, pkt.tag.stype,
			       pkt.tag.stype_env, pkt.tag.sid);
			rv = -ECONNRESET;
			break;
		}

		WARN_ON(rv != LDC_PACKET_SIZE);
	}

done:
	return rv;
}

static void vcc_rx_timer(struct timer_list *t)
{
	struct vcc_port *port = from_timer(port, t, rx_timer);
	struct vio_driver_state *vio;
	unsigned long flags;
	int rv;

	spin_lock_irqsave(&port->lock, flags);
	port->rx_timer.expires = 0;

	vio = &port->vio;

	enable_irq(vio->vdev->rx_irq);

	if (!port->tty || port->removed)
		goto done;

	rv = vcc_ldc_read(port);
	if (rv == -ECONNRESET)
		vio_conn_reset(vio);

done:
	spin_unlock_irqrestore(&port->lock, flags);
	vcc_put(port, false);
}

static void vcc_tx_timer(struct timer_list *t)
{
	struct vcc_port *port = from_timer(port, t, tx_timer);
	struct vio_vcc *pkt;
	unsigned long flags;
	size_t tosend = 0;
	int rv;

	spin_lock_irqsave(&port->lock, flags);
	port->tx_timer.expires = 0;

	if (!port->tty || port->removed)
		goto done;

	tosend = min(VCC_BUFF_LEN, port->chars_in_buffer);
	if (!tosend)
		goto done;

	pkt = &port->buffer;
	pkt->tag.type = VIO_TYPE_DATA;
	pkt->tag.stype = tosend;
	vccdbgl(port->vio.lp);

	rv = ldc_write(port->vio.lp, pkt, (VIO_TAG_SIZE + tosend));
	WARN_ON(!rv);

	if (rv < 0) {
		vccdbg("VCC: ldc_write()=%d\n", rv);
		vcc_kick_tx(port);
	} else {
		struct tty_struct *tty = port->tty;

		port->chars_in_buffer = 0;
		if (tty)
			tty_wakeup(tty);
	}

done:
	spin_unlock_irqrestore(&port->lock, flags);
	vcc_put(port, false);
}

/**
 * vcc_event() - LDC event processing engine
 * @arg: VCC private data
 * @event: LDC event
 *
 * Handles LDC events for VCC
 */
static void vcc_event(void *arg, int event)
{
	struct vio_driver_state *vio;
	struct vcc_port *port;
	unsigned long flags;
	int rv;

	port = arg;
	vio = &port->vio;

	spin_lock_irqsave(&port->lock, flags);

	switch (event) {
	case LDC_EVENT_RESET:
	case LDC_EVENT_UP:
		vio_link_state_change(vio, event);
		break;

	case LDC_EVENT_DATA_READY:
		rv = vcc_ldc_read(port);
		if (rv == -ECONNRESET)
			vio_conn_reset(vio);
		break;

	default:
		pr_err("VCC: unexpected LDC event(%d)\n", event);
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

static struct ldc_channel_config vcc_ldc_cfg = {
	.event		= vcc_event,
	.mtu		= VIO_VCC_MTU_SIZE,
	.mode		= LDC_MODE_RAW,
	.debug		= 0,
};

/* Ordered from largest major to lowest */
static struct vio_version vcc_versions[] = {
	{ .major = 1, .minor = 0 },
};

static struct tty_port_operations vcc_port_ops = { 0 };

static ssize_t domain_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct vcc_port *port;
	int rv;

	port = dev_get_drvdata(dev);
	if (!port)
		return -ENODEV;

	rv = scnprintf(buf, PAGE_SIZE, "%s\n", port->domain);

	return rv;
}

static int vcc_send_ctl(struct vcc_port *port, int ctl)
{
	struct vio_vcc pkt;
	int rv;

	pkt.tag.type = VIO_TYPE_CTRL;
	pkt.tag.sid = ctl;
	pkt.tag.stype = 0;

	rv = ldc_write(port->vio.lp, &pkt, sizeof(pkt.tag));
	WARN_ON(!rv);
	vccdbg("VCC: ldc_write(%ld)=%d\n", sizeof(pkt.tag), rv);

	return rv;
}

static ssize_t break_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct vcc_port *port;
	unsigned long flags;
	int rv = count;
	int brk;

	port = dev_get_drvdata(dev);
	if (!port)
		return -ENODEV;

	spin_lock_irqsave(&port->lock, flags);

	if (sscanf(buf, "%ud", &brk) != 1 || brk != 1)
		rv = -EINVAL;
	else if (vcc_send_ctl(port, VCC_CTL_BREAK) < 0)
		vcc_kick_tx(port);

	spin_unlock_irqrestore(&port->lock, flags);

	return rv;
}

static DEVICE_ATTR_ADMIN_RO(domain);
static DEVICE_ATTR_WO(break);

static struct attribute *vcc_sysfs_entries[] = {
	&dev_attr_domain.attr,
	&dev_attr_break.attr,
	NULL
};

static struct attribute_group vcc_attribute_group = {
	.name = NULL,
	.attrs = vcc_sysfs_entries,
};

/**
 * vcc_probe() - Initialize VCC port
 * @vdev: Pointer to VIO device of the new VCC port
 * @id: VIO device ID
 *
 * Initializes a VCC port to receive serial console data from
 * the guest domain. Sets up a TTY end point on the control
 * domain. Sets up VIO/LDC link between the guest & control
 * domain endpoints.
 *
 * Return: status of the probe
 */
static int vcc_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	struct mdesc_handle *hp;
	struct vcc_port *port;
	struct device *dev;
	const char *domain;
	char *name;
	u64 node;
	int rv;

	vccdbg("VCC: name=%s\n", dev_name(&vdev->dev));

	if (!vcc_tty_driver) {
		pr_err("VCC: TTY driver not registered\n");
		return -ENODEV;
	}

	port = kzalloc(sizeof(struct vcc_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	name = kstrdup(dev_name(&vdev->dev), GFP_KERNEL);
	if (!name) {
		rv = -ENOMEM;
		goto free_port;
	}

	rv = vio_driver_init(&port->vio, vdev, VDEV_CONSOLE_CON, vcc_versions,
			     ARRAY_SIZE(vcc_versions), NULL, name);
	if (rv)
		goto free_name;

	port->vio.debug = vcc_dbg_vio;
	vcc_ldc_cfg.debug = vcc_dbg_ldc;

	rv = vio_ldc_alloc(&port->vio, &vcc_ldc_cfg, port);
	if (rv)
		goto free_name;

	spin_lock_init(&port->lock);

	port->index = vcc_table_add(port);
	if (port->index == -1) {
		pr_err("VCC: no more TTY indices left for allocation\n");
		rv = -ENOMEM;
		goto free_ldc;
	}

	/* Register the device using VCC table index as TTY index */
	dev = tty_register_device(vcc_tty_driver, port->index, &vdev->dev);
	if (IS_ERR(dev)) {
		rv = PTR_ERR(dev);
		goto free_table;
	}

	hp = mdesc_grab();

	node = vio_vdev_node(hp, vdev);
	if (node == MDESC_NODE_NULL) {
		rv = -ENXIO;
		mdesc_release(hp);
		goto unreg_tty;
	}

	domain = mdesc_get_property(hp, node, "vcc-domain-name", NULL);
	if (!domain) {
		rv = -ENXIO;
		mdesc_release(hp);
		goto unreg_tty;
	}
	port->domain = kstrdup(domain, GFP_KERNEL);
	if (!port->domain) {
		rv = -ENOMEM;
		goto unreg_tty;
	}


	mdesc_release(hp);

	rv = sysfs_create_group(&vdev->dev.kobj, &vcc_attribute_group);
	if (rv)
		goto free_domain;

	timer_setup(&port->rx_timer, vcc_rx_timer, 0);
	timer_setup(&port->tx_timer, vcc_tx_timer, 0);

	dev_set_drvdata(&vdev->dev, port);

	/* It's possible to receive IRQs in the middle of vio_port_up. Disable
	 * IRQs until the port is up.
	 */
	disable_irq_nosync(vdev->rx_irq);
	vio_port_up(&port->vio);
	enable_irq(vdev->rx_irq);

	return 0;

free_domain:
	kfree(port->domain);
unreg_tty:
	tty_unregister_device(vcc_tty_driver, port->index);
free_table:
	vcc_table_remove(port->index);
free_ldc:
	vio_ldc_free(&port->vio);
free_name:
	kfree(name);
free_port:
	kfree(port);

	return rv;
}

/**
 * vcc_remove() - Terminate a VCC port
 * @vdev: Pointer to VIO device of the VCC port
 *
 * Terminates a VCC port. Sets up the teardown of TTY and
 * VIO/LDC link between guest and primary domains.
 *
 * Return: status of removal
 */
static void vcc_remove(struct vio_dev *vdev)
{
	struct vcc_port *port = dev_get_drvdata(&vdev->dev);

	del_timer_sync(&port->rx_timer);
	del_timer_sync(&port->tx_timer);

	/* If there's a process with the device open, do a synchronous
	 * hangup of the TTY. This *may* cause the process to call close
	 * asynchronously, but it's not guaranteed.
	 */
	if (port->tty)
		tty_vhangup(port->tty);

	/* Get exclusive reference to VCC, ensures that there are no other
	 * clients to this port. This cannot fail.
	 */
	vcc_get(port->index, true);

	tty_unregister_device(vcc_tty_driver, port->index);

	del_timer_sync(&port->vio.timer);
	vio_ldc_free(&port->vio);
	sysfs_remove_group(&vdev->dev.kobj, &vcc_attribute_group);
	dev_set_drvdata(&vdev->dev, NULL);
	if (port->tty) {
		port->removed = true;
		vcc_put(port, true);
	} else {
		vcc_table_remove(port->index);

		kfree(port->vio.name);
		kfree(port->domain);
		kfree(port);
	}
}

static const struct vio_device_id vcc_match[] = {
	{
		.type = "vcc-port",
	},
	{},
};
MODULE_DEVICE_TABLE(vio, vcc_match);

static struct vio_driver vcc_driver = {
	.id_table	= vcc_match,
	.probe		= vcc_probe,
	.remove		= vcc_remove,
	.name		= "vcc",
};

static int vcc_open(struct tty_struct *tty, struct file *vcc_file)
{
	struct vcc_port *port;

	if (tty->count > 1)
		return -EBUSY;

	port = vcc_get_ne(tty->index);
	if (unlikely(!port)) {
		pr_err("VCC: open: Failed to find VCC port\n");
		return -ENODEV;
	}

	if (unlikely(!port->vio.lp)) {
		pr_err("VCC: open: LDC channel not configured\n");
		vcc_put(port, false);
		return -EPIPE;
	}
	vccdbgl(port->vio.lp);

	vcc_put(port, false);

	if (unlikely(!tty->port)) {
		pr_err("VCC: open: TTY port not found\n");
		return -ENXIO;
	}

	if (unlikely(!tty->port->ops)) {
		pr_err("VCC: open: TTY ops not defined\n");
		return -ENXIO;
	}

	return tty_port_open(tty->port, tty, vcc_file);
}

static void vcc_close(struct tty_struct *tty, struct file *vcc_file)
{
	if (unlikely(tty->count > 1))
		return;

	if (unlikely(!tty->port)) {
		pr_err("VCC: close: TTY port not found\n");
		return;
	}

	tty_port_close(tty->port, tty, vcc_file);
}

static void vcc_ldc_hup(struct vcc_port *port)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	if (vcc_send_ctl(port, VCC_CTL_HUP) < 0)
		vcc_kick_tx(port);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void vcc_hangup(struct tty_struct *tty)
{
	struct vcc_port *port;

	port = vcc_get_ne(tty->index);
	if (unlikely(!port)) {
		pr_err("VCC: hangup: Failed to find VCC port\n");
		return;
	}

	if (unlikely(!tty->port)) {
		pr_err("VCC: hangup: TTY port not found\n");
		vcc_put(port, false);
		return;
	}

	vcc_ldc_hup(port);

	vcc_put(port, false);

	tty_port_hangup(tty->port);
}

static ssize_t vcc_write(struct tty_struct *tty, const u8 *buf, size_t count)
{
	struct vcc_port *port;
	struct vio_vcc *pkt;
	unsigned long flags;
	size_t total_sent = 0;
	size_t tosend = 0;
	int rv = -EINVAL;

	port = vcc_get_ne(tty->index);
	if (unlikely(!port)) {
		pr_err("VCC: write: Failed to find VCC port");
		return -ENODEV;
	}

	spin_lock_irqsave(&port->lock, flags);

	pkt = &port->buffer;
	pkt->tag.type = VIO_TYPE_DATA;

	while (count > 0) {
		/* Minimum of data to write and space available */
		tosend = min_t(size_t, count,
			       (VCC_BUFF_LEN - port->chars_in_buffer));

		if (!tosend)
			break;

		memcpy(&pkt->data[port->chars_in_buffer], &buf[total_sent],
		       tosend);
		port->chars_in_buffer += tosend;
		pkt->tag.stype = tosend;

		vccdbg("TAG [%02x:%02x:%04x:%08x]\n", pkt->tag.type,
		       pkt->tag.stype, pkt->tag.stype_env, pkt->tag.sid);
		vccdbg("DATA [%s]\n", pkt->data);
		vccdbgl(port->vio.lp);

		/* Since we know we have enough room in VCC buffer for tosend
		 * we record that it was sent regardless of whether the
		 * hypervisor actually took it because we have it buffered.
		 */
		rv = ldc_write(port->vio.lp, pkt, (VIO_TAG_SIZE + tosend));
		vccdbg("VCC: write: ldc_write(%zu)=%d\n",
		       (VIO_TAG_SIZE + tosend), rv);

		total_sent += tosend;
		count -= tosend;
		if (rv < 0) {
			vcc_kick_tx(port);
			break;
		}

		port->chars_in_buffer = 0;
	}

	spin_unlock_irqrestore(&port->lock, flags);

	vcc_put(port, false);

	vccdbg("VCC: write: total=%zu rv=%d", total_sent, rv);

	return total_sent ? total_sent : rv;
}

static unsigned int vcc_write_room(struct tty_struct *tty)
{
	struct vcc_port *port;
	unsigned int num;

	port = vcc_get_ne(tty->index);
	if (unlikely(!port)) {
		pr_err("VCC: write_room: Failed to find VCC port\n");
		return 0;
	}

	num = VCC_BUFF_LEN - port->chars_in_buffer;

	vcc_put(port, false);

	return num;
}

static unsigned int vcc_chars_in_buffer(struct tty_struct *tty)
{
	struct vcc_port *port;
	unsigned int num;

	port = vcc_get_ne(tty->index);
	if (unlikely(!port)) {
		pr_err("VCC: chars_in_buffer: Failed to find VCC port\n");
		return 0;
	}

	num = port->chars_in_buffer;

	vcc_put(port, false);

	return num;
}

static int vcc_break_ctl(struct tty_struct *tty, int state)
{
	struct vcc_port *port;
	unsigned long flags;

	port = vcc_get_ne(tty->index);
	if (unlikely(!port)) {
		pr_err("VCC: break_ctl: Failed to find VCC port\n");
		return -ENODEV;
	}

	/* Turn off break */
	if (state == 0) {
		vcc_put(port, false);
		return 0;
	}

	spin_lock_irqsave(&port->lock, flags);

	if (vcc_send_ctl(port, VCC_CTL_BREAK) < 0)
		vcc_kick_tx(port);

	spin_unlock_irqrestore(&port->lock, flags);

	vcc_put(port, false);

	return 0;
}

static int vcc_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct vcc_port *port_vcc;
	struct tty_port *port_tty;
	int ret;

	if (tty->index >= VCC_MAX_PORTS)
		return -EINVAL;

	ret = tty_standard_install(driver, tty);
	if (ret)
		return ret;

	port_tty = kzalloc(sizeof(struct tty_port), GFP_KERNEL);
	if (!port_tty)
		return -ENOMEM;

	port_vcc = vcc_get(tty->index, true);
	if (!port_vcc) {
		pr_err("VCC: install: Failed to find VCC port\n");
		tty->port = NULL;
		kfree(port_tty);
		return -ENODEV;
	}

	tty_port_init(port_tty);
	port_tty->ops = &vcc_port_ops;
	tty->port = port_tty;

	port_vcc->tty = tty;

	vcc_put(port_vcc, true);

	return 0;
}

static void vcc_cleanup(struct tty_struct *tty)
{
	struct vcc_port *port;

	port = vcc_get(tty->index, true);
	if (port) {
		port->tty = NULL;

		if (port->removed) {
			vcc_table_remove(tty->index);
			kfree(port->vio.name);
			kfree(port->domain);
			kfree(port);
		} else {
			vcc_put(port, true);
		}
	}

	tty_port_destroy(tty->port);
	kfree(tty->port);
	tty->port = NULL;
}

static const struct tty_operations vcc_ops = {
	.open			= vcc_open,
	.close			= vcc_close,
	.hangup			= vcc_hangup,
	.write			= vcc_write,
	.write_room		= vcc_write_room,
	.chars_in_buffer	= vcc_chars_in_buffer,
	.break_ctl		= vcc_break_ctl,
	.install		= vcc_install,
	.cleanup		= vcc_cleanup,
};

#define VCC_TTY_FLAGS   (TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_REAL_RAW)

static int vcc_tty_init(void)
{
	int rv;

	vcc_tty_driver = tty_alloc_driver(VCC_MAX_PORTS, VCC_TTY_FLAGS);
	if (IS_ERR(vcc_tty_driver)) {
		pr_err("VCC: TTY driver alloc failed\n");
		return PTR_ERR(vcc_tty_driver);
	}

	vcc_tty_driver->driver_name = "vcc";
	vcc_tty_driver->name = "vcc";

	vcc_tty_driver->minor_start = VCC_MINOR_START;
	vcc_tty_driver->type = TTY_DRIVER_TYPE_SYSTEM;
	vcc_tty_driver->init_termios = vcc_tty_termios;

	tty_set_operations(vcc_tty_driver, &vcc_ops);

	rv = tty_register_driver(vcc_tty_driver);
	if (rv) {
		pr_err("VCC: TTY driver registration failed\n");
		tty_driver_kref_put(vcc_tty_driver);
		vcc_tty_driver = NULL;
		return rv;
	}

	vccdbg("VCC: TTY driver registered\n");

	return 0;
}

static void vcc_tty_exit(void)
{
	tty_unregister_driver(vcc_tty_driver);
	tty_driver_kref_put(vcc_tty_driver);
	vccdbg("VCC: TTY driver unregistered\n");

	vcc_tty_driver = NULL;
}

static int __init vcc_init(void)
{
	int rv;

	rv = vcc_tty_init();
	if (rv) {
		pr_err("VCC: TTY init failed\n");
		return rv;
	}

	rv = vio_register_driver(&vcc_driver);
	if (rv) {
		pr_err("VCC: VIO driver registration failed\n");
		vcc_tty_exit();
	} else {
		vccdbg("VCC: VIO driver registered successfully\n");
	}

	return rv;
}

static void __exit vcc_exit(void)
{
	vio_unregister_driver(&vcc_driver);
	vccdbg("VCC: VIO driver unregistered\n");
	vcc_tty_exit();
	vccdbg("VCC: TTY driver unregistered\n");
}

module_init(vcc_init);
module_exit(vcc_exit);
