/*
 * pcmcia_ioctl.c -- ioctl interface for cardmgr and cardctl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 * (C) 2003 - 2004	Dominik Brodowski
 */

/*
 * This file will go away soon.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/smp_lock.h>
#include <linux/workqueue.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include <pcmcia/ss.h>

#include "cs_internal.h"

static int major_dev = -1;


/* Device user information */
#define MAX_EVENTS	32
#define USER_MAGIC	0x7ea4
#define CHECK_USER(u) \
    (((u) == NULL) || ((u)->user_magic != USER_MAGIC))

typedef struct user_info_t {
	u_int			user_magic;
	int			event_head, event_tail;
	event_t			event[MAX_EVENTS];
	struct user_info_t	*next;
	struct pcmcia_socket	*socket;
} user_info_t;


#ifdef CONFIG_PCMCIA_DEBUG
extern int ds_pc_debug;

#define ds_dbg(lvl, fmt, arg...) do {		\
	if (ds_pc_debug >= lvl)				\
		printk(KERN_DEBUG "ds: " fmt , ## arg);		\
} while (0)
#else
#define ds_dbg(lvl, fmt, arg...) do { } while (0)
#endif

static struct pcmcia_device *get_pcmcia_device(struct pcmcia_socket *s,
						unsigned int function)
{
	struct pcmcia_device *p_dev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
	list_for_each_entry(p_dev, &s->devices_list, socket_device_list) {
		if (p_dev->func == function) {
			spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
			return pcmcia_get_dev(p_dev);
		}
	}
	spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
	return NULL;
}

/* backwards-compatible accessing of driver --- by name! */

static struct pcmcia_driver *get_pcmcia_driver(dev_info_t *dev_info)
{
	struct device_driver *drv;
	struct pcmcia_driver *p_drv;

	drv = driver_find((char *) dev_info, &pcmcia_bus_type);
	if (!drv)
		return NULL;

	p_drv = container_of(drv, struct pcmcia_driver, drv);

	return (p_drv);
}


#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_pccard = NULL;

static int proc_read_drivers_callback(struct device_driver *driver, void *_m)
{
	struct seq_file *m = _m;
	struct pcmcia_driver *p_drv = container_of(driver,
						   struct pcmcia_driver, drv);

	seq_printf(m, "%-24.24s 1 %d\n", p_drv->drv.name,
#ifdef CONFIG_MODULE_UNLOAD
		      (p_drv->owner) ? module_refcount(p_drv->owner) : 1
#else
		      1
#endif
	);
	return 0;
}

static int pccard_drivers_proc_show(struct seq_file *m, void *v)
{
	return bus_for_each_drv(&pcmcia_bus_type, NULL,
				m, proc_read_drivers_callback);
}

static int pccard_drivers_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pccard_drivers_proc_show, NULL);
}

static const struct file_operations pccard_drivers_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= pccard_drivers_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif


#ifdef CONFIG_PCMCIA_PROBE

static int adjust_irq(struct pcmcia_socket *s, adjust_t *adj)
{
	int irq;
	u32 mask;

	irq = adj->resource.irq.IRQ;
	if ((irq < 0) || (irq > 15))
		return -EINVAL;

	if (adj->Action != REMOVE_MANAGED_RESOURCE)
		return 0;

	mask = 1 << irq;

	if (!(s->irq_mask & mask))
		return 0;

	s->irq_mask &= ~mask;

	return 0;
}

#else

static inline int adjust_irq(struct pcmcia_socket *s, adjust_t *adj) {
	return 0;
}

#endif

static int pcmcia_adjust_resource_info(adjust_t *adj)
{
	struct pcmcia_socket *s;
	int ret = -ENOSYS;
	unsigned long flags;

	down_read(&pcmcia_socket_list_rwsem);
	list_for_each_entry(s, &pcmcia_socket_list, socket_list) {

		if (adj->Resource == RES_IRQ)
			ret = adjust_irq(s, adj);

		else if (s->resource_ops->add_io) {
			unsigned long begin, end;

			/* you can't use the old interface if the new
			 * one was used before */
			spin_lock_irqsave(&s->lock, flags);
			if ((s->resource_setup_new) &&
			    !(s->resource_setup_old)) {
				spin_unlock_irqrestore(&s->lock, flags);
				continue;
			} else if (!(s->resource_setup_old))
				s->resource_setup_old = 1;
			spin_unlock_irqrestore(&s->lock, flags);

			switch (adj->Resource) {
			case RES_MEMORY_RANGE:
				begin = adj->resource.memory.Base;
				end = adj->resource.memory.Base + adj->resource.memory.Size - 1;
				if (s->resource_ops->add_mem)
					ret =s->resource_ops->add_mem(s, adj->Action, begin, end);
			case RES_IO_RANGE:
				begin = adj->resource.io.BasePort;
				end = adj->resource.io.BasePort + adj->resource.io.NumPorts - 1;
				if (s->resource_ops->add_io)
					ret = s->resource_ops->add_io(s, adj->Action, begin, end);
			}
			if (!ret) {
				/* as there's no way we know this is the
				 * last call to adjust_resource_info, we
				 * always need to assume this is the latest
				 * one... */
				spin_lock_irqsave(&s->lock, flags);
				s->resource_setup_done = 1;
				spin_unlock_irqrestore(&s->lock, flags);
			}
		}
	}
	up_read(&pcmcia_socket_list_rwsem);

	return (ret);
}

/** pccard_get_status
 *
 * Get the current socket state bits.  We don't support the latched
 * SocketState yet: I haven't seen any point for it.
 */

static int pccard_get_status(struct pcmcia_socket *s,
			     struct pcmcia_device *p_dev,
			     cs_status_t *status)
{
	config_t *c;
	int val;

	s->ops->get_status(s, &val);
	status->CardState = status->SocketState = 0;
	status->CardState |= (val & SS_DETECT) ? CS_EVENT_CARD_DETECT : 0;
	status->CardState |= (val & SS_CARDBUS) ? CS_EVENT_CB_DETECT : 0;
	status->CardState |= (val & SS_3VCARD) ? CS_EVENT_3VCARD : 0;
	status->CardState |= (val & SS_XVCARD) ? CS_EVENT_XVCARD : 0;
	if (s->state & SOCKET_SUSPEND)
		status->CardState |= CS_EVENT_PM_SUSPEND;
	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;

	c = (p_dev) ? p_dev->function_config : NULL;

	if ((c != NULL) && (c->state & CONFIG_LOCKED) &&
	    (c->IntType & (INT_MEMORY_AND_IO | INT_ZOOMED_VIDEO))) {
		u_char reg;
		if (c->CardValues & PRESENT_PIN_REPLACE) {
			pcmcia_read_cis_mem(s, 1, (c->ConfigBase+CISREG_PRR)>>1, 1, &reg);
			status->CardState |=
				(reg & PRR_WP_STATUS) ? CS_EVENT_WRITE_PROTECT : 0;
			status->CardState |=
				(reg & PRR_READY_STATUS) ? CS_EVENT_READY_CHANGE : 0;
			status->CardState |=
				(reg & PRR_BVD2_STATUS) ? CS_EVENT_BATTERY_LOW : 0;
			status->CardState |=
				(reg & PRR_BVD1_STATUS) ? CS_EVENT_BATTERY_DEAD : 0;
		} else {
			/* No PRR?  Then assume we're always ready */
			status->CardState |= CS_EVENT_READY_CHANGE;
		}
		if (c->CardValues & PRESENT_EXT_STATUS) {
			pcmcia_read_cis_mem(s, 1, (c->ConfigBase+CISREG_ESR)>>1, 1, &reg);
			status->CardState |=
				(reg & ESR_REQ_ATTN) ? CS_EVENT_REQUEST_ATTENTION : 0;
		}
		return 0;
	}
	status->CardState |=
		(val & SS_WRPROT) ? CS_EVENT_WRITE_PROTECT : 0;
	status->CardState |=
		(val & SS_BATDEAD) ? CS_EVENT_BATTERY_DEAD : 0;
	status->CardState |=
		(val & SS_BATWARN) ? CS_EVENT_BATTERY_LOW : 0;
	status->CardState |=
		(val & SS_READY) ? CS_EVENT_READY_CHANGE : 0;
	return 0;
} /* pccard_get_status */

static int pccard_get_configuration_info(struct pcmcia_socket *s,
				  struct pcmcia_device *p_dev,
				  config_info_t *config)
{
	config_t *c;

	if (!(s->state & SOCKET_PRESENT))
		return -ENODEV;


#ifdef CONFIG_CARDBUS
	if (s->state & SOCKET_CARDBUS) {
		memset(config, 0, sizeof(config_info_t));
		config->Vcc = s->socket.Vcc;
		config->Vpp1 = config->Vpp2 = s->socket.Vpp;
		config->Option = s->cb_dev->subordinate->number;
		if (s->state & SOCKET_CARDBUS_CONFIG) {
			config->Attributes = CONF_VALID_CLIENT;
			config->IntType = INT_CARDBUS;
			config->AssignedIRQ = s->irq.AssignedIRQ;
			if (config->AssignedIRQ)
				config->Attributes |= CONF_ENABLE_IRQ;
			if (s->io[0].res) {
				config->BasePort1 = s->io[0].res->start;
				config->NumPorts1 = s->io[0].res->end -
					config->BasePort1 + 1;
			}
		}
		return 0;
	}
#endif

	if (p_dev) {
		c = p_dev->function_config;
		config->Function = p_dev->func;
	} else {
		c = NULL;
		config->Function = 0;
	}

	if ((c == NULL) || !(c->state & CONFIG_LOCKED)) {
		config->Attributes = 0;
		config->Vcc = s->socket.Vcc;
		config->Vpp1 = config->Vpp2 = s->socket.Vpp;
		return 0;
	}

	config->Attributes = c->Attributes | CONF_VALID_CLIENT;
	config->Vcc = s->socket.Vcc;
	config->Vpp1 = config->Vpp2 = s->socket.Vpp;
	config->IntType = c->IntType;
	config->ConfigBase = c->ConfigBase;
	config->Status = c->Status;
	config->Pin = c->Pin;
	config->Copy = c->Copy;
	config->Option = c->Option;
	config->ExtStatus = c->ExtStatus;
	config->Present = config->CardValues = c->CardValues;
	config->IRQAttributes = c->irq.Attributes;
	config->AssignedIRQ = s->irq.AssignedIRQ;
	config->BasePort1 = c->io.BasePort1;
	config->NumPorts1 = c->io.NumPorts1;
	config->Attributes1 = c->io.Attributes1;
	config->BasePort2 = c->io.BasePort2;
	config->NumPorts2 = c->io.NumPorts2;
	config->Attributes2 = c->io.Attributes2;
	config->IOAddrLines = c->io.IOAddrLines;

	return 0;
} /* pccard_get_configuration_info */


/*======================================================================

    These manage a ring buffer of events pending for one user process

======================================================================*/


static int queue_empty(user_info_t *user)
{
    return (user->event_head == user->event_tail);
}

static event_t get_queued_event(user_info_t *user)
{
    user->event_tail = (user->event_tail+1) % MAX_EVENTS;
    return user->event[user->event_tail];
}

static void queue_event(user_info_t *user, event_t event)
{
    user->event_head = (user->event_head+1) % MAX_EVENTS;
    if (user->event_head == user->event_tail)
	user->event_tail = (user->event_tail+1) % MAX_EVENTS;
    user->event[user->event_head] = event;
}

void handle_event(struct pcmcia_socket *s, event_t event)
{
    user_info_t *user;
    for (user = s->user; user; user = user->next)
	queue_event(user, event);
    wake_up_interruptible(&s->queue);
}


/*======================================================================

    bind_request() and bind_device() are merged by now. Register_client()
    is called right at the end of bind_request(), during the driver's
    ->attach() call. Individual descriptions:

    bind_request() connects a socket to a particular client driver.
    It looks up the specified device ID in the list of registered
    drivers, binds it to the socket, and tries to create an instance
    of the device.  unbind_request() deletes a driver instance.

    Bind_device() associates a device driver with a particular socket.
    It is normally called by Driver Services after it has identified
    a newly inserted card.  An instance of that driver will then be
    eligible to register as a client of this socket.

    Register_client() uses the dev_info_t handle to match the
    caller with a socket.  The driver must have already been bound
    to a socket with bind_device() -- in fact, bind_device()
    allocates the client structure that will be used.

======================================================================*/

static int bind_request(struct pcmcia_socket *s, bind_info_t *bind_info)
{
	struct pcmcia_driver *p_drv;
	struct pcmcia_device *p_dev;
	int ret = 0;
	unsigned long flags;

	s = pcmcia_get_socket(s);
	if (!s)
		return -EINVAL;

	ds_dbg(2, "bind_request(%d, '%s')\n", s->sock,
	       (char *)bind_info->dev_info);

	p_drv = get_pcmcia_driver(&bind_info->dev_info);
	if (!p_drv) {
		ret = -EINVAL;
		goto err_put;
	}

	if (!try_module_get(p_drv->owner)) {
		ret = -EINVAL;
		goto err_put_driver;
	}

	spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
        list_for_each_entry(p_dev, &s->devices_list, socket_device_list) {
		if (p_dev->func == bind_info->function) {
			if ((p_dev->dev.driver == &p_drv->drv)) {
				if (p_dev->cardmgr) {
					/* if there's already a device
					 * registered, and it was registered
					 * by userspace before, we need to
					 * return the "instance". */
					spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
					bind_info->instance = p_dev;
					ret = -EBUSY;
					goto err_put_module;
				} else {
					/* the correct driver managed to bind
					 * itself magically to the correct
					 * device. */
					spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
					p_dev->cardmgr = p_drv;
					ret = 0;
					goto err_put_module;
				}
			} else if (!p_dev->dev.driver) {
				/* there's already a device available where
				 * no device has been bound to yet. So we don't
				 * need to register a device! */
				spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
				goto rescan;
			}
		}
	}
	spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);

	p_dev = pcmcia_device_add(s, bind_info->function);
	if (!p_dev) {
		ret = -EIO;
		goto err_put_module;
	}

rescan:
	p_dev->cardmgr = p_drv;

	/* if a driver is already running, we can abort */
	if (p_dev->dev.driver)
		goto err_put_module;

	/*
	 * Prevent this racing with a card insertion.
	 */
	mutex_lock(&s->skt_mutex);
	ret = bus_rescan_devices(&pcmcia_bus_type);
	mutex_unlock(&s->skt_mutex);
	if (ret)
		goto err_put_module;

	/* check whether the driver indeed matched. I don't care if this
	 * is racy or not, because it can only happen on cardmgr access
	 * paths...
	 */
	if (!(p_dev->dev.driver == &p_drv->drv))
		p_dev->cardmgr = NULL;

 err_put_module:
	module_put(p_drv->owner);
 err_put_driver:
	put_driver(&p_drv->drv);
 err_put:
	pcmcia_put_socket(s);

	return (ret);
} /* bind_request */

#ifdef CONFIG_CARDBUS

static struct pci_bus *pcmcia_lookup_bus(struct pcmcia_socket *s)
{
	if (!s || !(s->state & SOCKET_CARDBUS))
		return NULL;

	return s->cb_dev->subordinate;
}
#endif

static int get_device_info(struct pcmcia_socket *s, bind_info_t *bind_info, int first)
{
	dev_node_t *node;
	struct pcmcia_device *p_dev;
	struct pcmcia_driver *p_drv;
	unsigned long flags;
	int ret = 0;

#ifdef CONFIG_CARDBUS
	/*
	 * Some unbelievably ugly code to associate the PCI cardbus
	 * device and its driver with the PCMCIA "bind" information.
	 */
	{
		struct pci_bus *bus;

		bus = pcmcia_lookup_bus(s);
		if (bus) {
			struct list_head *list;
			struct pci_dev *dev = NULL;

			list = bus->devices.next;
			while (list != &bus->devices) {
				struct pci_dev *pdev = pci_dev_b(list);
				list = list->next;

				if (first) {
					dev = pdev;
					break;
				}

				/* Try to handle "next" here some way? */
			}
			if (dev && dev->driver) {
				strlcpy(bind_info->name, dev->driver->name, DEV_NAME_LEN);
				bind_info->major = 0;
				bind_info->minor = 0;
				bind_info->next = NULL;
				return 0;
			}
		}
	}
#endif

	spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
	list_for_each_entry(p_dev, &s->devices_list, socket_device_list) {
		if (p_dev->func == bind_info->function) {
			p_dev = pcmcia_get_dev(p_dev);
			if (!p_dev)
				continue;
			goto found;
		}
	}
	spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
	return -ENODEV;

 found:
	spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);

	p_drv = to_pcmcia_drv(p_dev->dev.driver);
	if (p_drv && !p_dev->_locked) {
		ret = -EAGAIN;
		goto err_put;
	}

	if (first)
		node = p_dev->dev_node;
	else
		for (node = p_dev->dev_node; node; node = node->next)
			if (node == bind_info->next)
				break;
	if (!node) {
		ret = -ENODEV;
		goto err_put;
	}

	strlcpy(bind_info->name, node->dev_name, DEV_NAME_LEN);
	bind_info->major = node->major;
	bind_info->minor = node->minor;
	bind_info->next = node->next;

 err_put:
	pcmcia_put_dev(p_dev);
	return (ret);
} /* get_device_info */


static int ds_open(struct inode *inode, struct file *file)
{
    socket_t i = iminor(inode);
    struct pcmcia_socket *s;
    user_info_t *user;
    static int warning_printed = 0;
    int ret = 0;

    ds_dbg(0, "ds_open(socket %d)\n", i);

    lock_kernel();
    s = pcmcia_get_socket_by_nr(i);
    if (!s) {
	    ret = -ENODEV;
	    goto out;
    }
    s = pcmcia_get_socket(s);
    if (!s) {
	    ret = -ENODEV;
	    goto out;
    }

    if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
	    if (s->pcmcia_state.busy) {
		    pcmcia_put_socket(s);
		    ret = -EBUSY;
		    goto out;
	    }
	else
	    s->pcmcia_state.busy = 1;
    }

    user = kmalloc(sizeof(user_info_t), GFP_KERNEL);
    if (!user) {
	    pcmcia_put_socket(s);
	    ret = -ENOMEM;
	    goto out;
    }
    user->event_tail = user->event_head = 0;
    user->next = s->user;
    user->user_magic = USER_MAGIC;
    user->socket = s;
    s->user = user;
    file->private_data = user;

    if (!warning_printed) {
	    printk(KERN_INFO "pcmcia: Detected deprecated PCMCIA ioctl "
			"usage from process: %s.\n", current->comm);
	    printk(KERN_INFO "pcmcia: This interface will soon be removed from "
			"the kernel; please expect breakage unless you upgrade "
			"to new tools.\n");
	    printk(KERN_INFO "pcmcia: see http://www.kernel.org/pub/linux/"
			"utils/kernel/pcmcia/pcmcia.html for details.\n");
	    warning_printed = 1;
    }

    if (s->pcmcia_state.present)
	queue_event(user, CS_EVENT_CARD_INSERTION);
out:
    unlock_kernel();
    return ret;
} /* ds_open */

/*====================================================================*/

static int ds_release(struct inode *inode, struct file *file)
{
    struct pcmcia_socket *s;
    user_info_t *user, **link;

    ds_dbg(0, "ds_release(socket %d)\n", iminor(inode));

    user = file->private_data;
    if (CHECK_USER(user))
	goto out;

    s = user->socket;

    /* Unlink user data structure */
    if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
	s->pcmcia_state.busy = 0;
    }
    file->private_data = NULL;
    for (link = &s->user; *link; link = &(*link)->next)
	if (*link == user) break;
    if (link == NULL)
	goto out;
    *link = user->next;
    user->user_magic = 0;
    kfree(user);
    pcmcia_put_socket(s);
out:
    return 0;
} /* ds_release */

/*====================================================================*/

static ssize_t ds_read(struct file *file, char __user *buf,
		       size_t count, loff_t *ppos)
{
    struct pcmcia_socket *s;
    user_info_t *user;
    int ret;

    ds_dbg(2, "ds_read(socket %d)\n", iminor(file->f_path.dentry->d_inode));

    if (count < 4)
	return -EINVAL;

    user = file->private_data;
    if (CHECK_USER(user))
	return -EIO;

    s = user->socket;
    if (s->pcmcia_state.dead)
        return -EIO;

    ret = wait_event_interruptible(s->queue, !queue_empty(user));
    if (ret == 0)
	ret = put_user(get_queued_event(user), (int __user *)buf) ? -EFAULT : 4;

    return ret;
} /* ds_read */

/*====================================================================*/

static ssize_t ds_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
    ds_dbg(2, "ds_write(socket %d)\n", iminor(file->f_path.dentry->d_inode));

    if (count != 4)
	return -EINVAL;
    if ((file->f_flags & O_ACCMODE) == O_RDONLY)
	return -EBADF;

    return -EIO;
} /* ds_write */

/*====================================================================*/

/* No kernel lock - fine */
static u_int ds_poll(struct file *file, poll_table *wait)
{
    struct pcmcia_socket *s;
    user_info_t *user;

    ds_dbg(2, "ds_poll(socket %d)\n", iminor(file->f_path.dentry->d_inode));

    user = file->private_data;
    if (CHECK_USER(user))
	return POLLERR;
    s = user->socket;
    /*
     * We don't check for a dead socket here since that
     * will send cardmgr into an endless spin.
     */
    poll_wait(file, &s->queue, wait);
    if (!queue_empty(user))
	return POLLIN | POLLRDNORM;
    return 0;
} /* ds_poll */

/*====================================================================*/

static int ds_ioctl(struct inode * inode, struct file * file,
		    u_int cmd, u_long arg)
{
    struct pcmcia_socket *s;
    void __user *uarg = (char __user *)arg;
    u_int size;
    int ret, err;
    ds_ioctl_arg_t *buf;
    user_info_t *user;

    ds_dbg(2, "ds_ioctl(socket %d, %#x, %#lx)\n", iminor(inode), cmd, arg);

    user = file->private_data;
    if (CHECK_USER(user))
	return -EIO;

    s = user->socket;
    if (s->pcmcia_state.dead)
        return -EIO;

    size = (cmd & IOCSIZE_MASK) >> IOCSIZE_SHIFT;
    if (size > sizeof(ds_ioctl_arg_t)) return -EINVAL;

    /* Permission check */
    if (!(cmd & IOC_OUT) && !capable(CAP_SYS_ADMIN))
	return -EPERM;

    if (cmd & IOC_IN) {
	if (!access_ok(VERIFY_READ, uarg, size)) {
	    ds_dbg(3, "ds_ioctl(): verify_read = %d\n", -EFAULT);
	    return -EFAULT;
	}
    }
    if (cmd & IOC_OUT) {
	if (!access_ok(VERIFY_WRITE, uarg, size)) {
	    ds_dbg(3, "ds_ioctl(): verify_write = %d\n", -EFAULT);
	    return -EFAULT;
	}
    }
    buf = kmalloc(sizeof(ds_ioctl_arg_t), GFP_KERNEL);
    if (!buf)
	return -ENOMEM;

    err = ret = 0;

    if (cmd & IOC_IN) {
	if (__copy_from_user((char *)buf, uarg, size)) {
	    err = -EFAULT;
	    goto free_out;
	}
    }

    switch (cmd) {
    case DS_ADJUST_RESOURCE_INFO:
	ret = pcmcia_adjust_resource_info(&buf->adjust);
	break;
    case DS_GET_CONFIGURATION_INFO:
	if (buf->config.Function &&
	   (buf->config.Function >= s->functions))
	    ret = -EINVAL;
	else {
	    struct pcmcia_device *p_dev = get_pcmcia_device(s, buf->config.Function);
	    ret = pccard_get_configuration_info(s, p_dev, &buf->config);
	    pcmcia_put_dev(p_dev);
	}
	break;
    case DS_GET_FIRST_TUPLE:
	mutex_lock(&s->skt_mutex);
	pcmcia_validate_mem(s);
	mutex_unlock(&s->skt_mutex);
	ret = pccard_get_first_tuple(s, BIND_FN_ALL, &buf->tuple);
	break;
    case DS_GET_NEXT_TUPLE:
	ret = pccard_get_next_tuple(s, BIND_FN_ALL, &buf->tuple);
	break;
    case DS_GET_TUPLE_DATA:
	buf->tuple.TupleData = buf->tuple_parse.data;
	buf->tuple.TupleDataMax = sizeof(buf->tuple_parse.data);
	ret = pccard_get_tuple_data(s, &buf->tuple);
	break;
    case DS_PARSE_TUPLE:
	buf->tuple.TupleData = buf->tuple_parse.data;
	ret = pcmcia_parse_tuple(&buf->tuple, &buf->tuple_parse.parse);
	break;
    case DS_RESET_CARD:
	ret = pcmcia_reset_card(s);
	break;
    case DS_GET_STATUS:
	    if (buf->status.Function &&
		(buf->status.Function >= s->functions))
		    ret = -EINVAL;
	    else {
		    struct pcmcia_device *p_dev = get_pcmcia_device(s, buf->status.Function);
		    ret = pccard_get_status(s, p_dev, &buf->status);
		    pcmcia_put_dev(p_dev);
	    }
	    break;
    case DS_VALIDATE_CIS:
	mutex_lock(&s->skt_mutex);
	pcmcia_validate_mem(s);
	mutex_unlock(&s->skt_mutex);
	ret = pccard_validate_cis(s, &buf->cisinfo.Chains);
	break;
    case DS_SUSPEND_CARD:
	ret = pcmcia_suspend_card(s);
	break;
    case DS_RESUME_CARD:
	ret = pcmcia_resume_card(s);
	break;
    case DS_EJECT_CARD:
	err = pcmcia_eject_card(s);
	break;
    case DS_INSERT_CARD:
	err = pcmcia_insert_card(s);
	break;
    case DS_ACCESS_CONFIGURATION_REGISTER:
	if ((buf->conf_reg.Action == CS_WRITE) && !capable(CAP_SYS_ADMIN)) {
	    err = -EPERM;
	    goto free_out;
	}

	ret = -EINVAL;

	if (!(buf->conf_reg.Function &&
	     (buf->conf_reg.Function >= s->functions))) {
		struct pcmcia_device *p_dev = get_pcmcia_device(s, buf->conf_reg.Function);
		if (p_dev) {
			ret = pcmcia_access_configuration_register(p_dev, &buf->conf_reg);
			pcmcia_put_dev(p_dev);
		}
	}
	break;
    case DS_GET_FIRST_REGION:
    case DS_GET_NEXT_REGION:
    case DS_BIND_MTD:
	if (!capable(CAP_SYS_ADMIN)) {
		err = -EPERM;
		goto free_out;
	} else {
			printk_once(KERN_WARNING
				"2.6. kernels use pcmciamtd instead of memory_cs.c and do not require special\n");
			printk_once(KERN_WARNING "MTD handling any more.\n");
	}
	err = -EINVAL;
	goto free_out;
	break;
    case DS_GET_FIRST_WINDOW:
	ret = pcmcia_get_window(s, &buf->win_info.handle, 0,
			&buf->win_info.window);
	break;
    case DS_GET_NEXT_WINDOW:
	ret = pcmcia_get_window(s, &buf->win_info.handle,
			buf->win_info.handle->index + 1, &buf->win_info.window);
	break;
    case DS_GET_MEM_PAGE:
	ret = pcmcia_get_mem_page(buf->win_info.handle,
			   &buf->win_info.map);
	break;
    case DS_REPLACE_CIS:
	ret = pcmcia_replace_cis(s, buf->cisdump.Data, buf->cisdump.Length);
	break;
    case DS_BIND_REQUEST:
	if (!capable(CAP_SYS_ADMIN)) {
		err = -EPERM;
		goto free_out;
	}
	err = bind_request(s, &buf->bind_info);
	break;
    case DS_GET_DEVICE_INFO:
	err = get_device_info(s, &buf->bind_info, 1);
	break;
    case DS_GET_NEXT_DEVICE:
	err = get_device_info(s, &buf->bind_info, 0);
	break;
    case DS_UNBIND_REQUEST:
	err = 0;
	break;
    default:
	err = -EINVAL;
    }

    if ((err == 0) && (ret != 0)) {
	ds_dbg(2, "ds_ioctl: ret = %d\n", ret);
	switch (ret) {
	case -ENODEV:
	case -EINVAL:
	case -EBUSY:
	case -ENOSYS:
	    err = ret;
	    break;
	case -ENOMEM:
	    err = -ENOSPC; break;
	case -ENOSPC:
	    err = -ENODATA; break;
	default:
	    err = -EIO; break;
	}
    }

    if (cmd & IOC_OUT) {
        if (__copy_to_user(uarg, (char *)buf, size))
            err = -EFAULT;
    }

free_out:
    kfree(buf);
    return err;
} /* ds_ioctl */

/*====================================================================*/

static const struct file_operations ds_fops = {
	.owner		= THIS_MODULE,
	.open		= ds_open,
	.release	= ds_release,
	.ioctl		= ds_ioctl,
	.read		= ds_read,
	.write		= ds_write,
	.poll		= ds_poll,
};

void __init pcmcia_setup_ioctl(void) {
	int i;

	/* Set up character device for user mode clients */
	i = register_chrdev(0, "pcmcia", &ds_fops);
	if (i < 0)
		printk(KERN_NOTICE "unable to find a free device # for "
		       "Driver Services (error=%d)\n", i);
	else
		major_dev = i;

#ifdef CONFIG_PROC_FS
	proc_pccard = proc_mkdir("bus/pccard", NULL);
	if (proc_pccard)
		proc_create("drivers", 0, proc_pccard, &pccard_drivers_proc_fops);
#endif
}


void __exit pcmcia_cleanup_ioctl(void) {
#ifdef CONFIG_PROC_FS
	if (proc_pccard) {
		remove_proc_entry("drivers", proc_pccard);
		remove_proc_entry("bus/pccard", NULL);
	}
#endif
	if (major_dev != -1)
		unregister_chrdev(major_dev, "pcmcia");
}
