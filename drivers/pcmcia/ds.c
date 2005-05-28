/*
 * ds.c -- 16-bit PCMCIA core support
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/timer.h>
#include <linux/ioctl.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/kref.h>
#include <linux/workqueue.h>

#include <asm/atomic.h>

#define IN_CARD_SERVICES
#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/ss.h>

#include "cs_internal.h"

/*====================================================================*/

/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("PCMCIA Driver Services");
MODULE_LICENSE("GPL");

#ifdef DEBUG
int ds_pc_debug;

module_param_named(pc_debug, ds_pc_debug, int, 0644);

#define ds_dbg(lvl, fmt, arg...) do {				\
	if (ds_pc_debug > (lvl))					\
		printk(KERN_DEBUG "ds: " fmt , ## arg);		\
} while (0)
#else
#define ds_dbg(lvl, fmt, arg...) do { } while (0)
#endif

/*====================================================================*/

/* Device user information */
#define MAX_EVENTS	32
#define USER_MAGIC	0x7ea4
#define CHECK_USER(u) \
    (((u) == NULL) || ((u)->user_magic != USER_MAGIC))
typedef struct user_info_t {
    u_int		user_magic;
    int			event_head, event_tail;
    event_t		event[MAX_EVENTS];
    struct user_info_t	*next;
    struct pcmcia_bus_socket *socket;
} user_info_t;

/* Socket state information */
struct pcmcia_bus_socket {
	struct kref		refcount;
	struct pcmcia_callback	callback;
	int			state;
	user_info_t		*user;
	wait_queue_head_t	queue;
	struct pcmcia_socket	*parent;

	/* the PCMCIA devices connected to this socket (normally one, more
	 * for multifunction devices: */
	struct list_head	devices_list;
	u8			device_count; /* the number of devices, used
					       * only internally and subject
					       * to incorrectness and change */
};
static spinlock_t pcmcia_dev_list_lock;

#define DS_SOCKET_PRESENT		0x01
#define DS_SOCKET_BUSY			0x02
#define DS_SOCKET_REMOVAL_PENDING	0x10
#define DS_SOCKET_DEAD			0x80

/*====================================================================*/

static int major_dev = -1;

static int unbind_request(struct pcmcia_bus_socket *s);

/*====================================================================*/

/* code which was in cs.c before */

/* String tables for error messages */

typedef struct lookup_t {
    int key;
    char *msg;
} lookup_t;

static const lookup_t error_table[] = {
    { CS_SUCCESS,		"Operation succeeded" },
    { CS_BAD_ADAPTER,		"Bad adapter" },
    { CS_BAD_ATTRIBUTE, 	"Bad attribute", },
    { CS_BAD_BASE,		"Bad base address" },
    { CS_BAD_EDC,		"Bad EDC" },
    { CS_BAD_IRQ,		"Bad IRQ" },
    { CS_BAD_OFFSET,		"Bad offset" },
    { CS_BAD_PAGE,		"Bad page number" },
    { CS_READ_FAILURE,		"Read failure" },
    { CS_BAD_SIZE,		"Bad size" },
    { CS_BAD_SOCKET,		"Bad socket" },
    { CS_BAD_TYPE,		"Bad type" },
    { CS_BAD_VCC,		"Bad Vcc" },
    { CS_BAD_VPP,		"Bad Vpp" },
    { CS_BAD_WINDOW,		"Bad window" },
    { CS_WRITE_FAILURE,		"Write failure" },
    { CS_NO_CARD,		"No card present" },
    { CS_UNSUPPORTED_FUNCTION,	"Usupported function" },
    { CS_UNSUPPORTED_MODE,	"Unsupported mode" },
    { CS_BAD_SPEED,		"Bad speed" },
    { CS_BUSY,			"Resource busy" },
    { CS_GENERAL_FAILURE,	"General failure" },
    { CS_WRITE_PROTECTED,	"Write protected" },
    { CS_BAD_ARG_LENGTH,	"Bad argument length" },
    { CS_BAD_ARGS,		"Bad arguments" },
    { CS_CONFIGURATION_LOCKED,	"Configuration locked" },
    { CS_IN_USE,		"Resource in use" },
    { CS_NO_MORE_ITEMS,		"No more items" },
    { CS_OUT_OF_RESOURCE,	"Out of resource" },
    { CS_BAD_HANDLE,		"Bad handle" },
    { CS_BAD_TUPLE,		"Bad CIS tuple" }
};


static const lookup_t service_table[] = {
    { AccessConfigurationRegister,	"AccessConfigurationRegister" },
    { AddSocketServices,		"AddSocketServices" },
    { AdjustResourceInfo,		"AdjustResourceInfo" },
    { CheckEraseQueue,			"CheckEraseQueue" },
    { CloseMemory,			"CloseMemory" },
    { DeregisterClient,			"DeregisterClient" },
    { DeregisterEraseQueue,		"DeregisterEraseQueue" },
    { GetCardServicesInfo,		"GetCardServicesInfo" },
    { GetClientInfo,			"GetClientInfo" },
    { GetConfigurationInfo,		"GetConfigurationInfo" },
    { GetEventMask,			"GetEventMask" },
    { GetFirstClient,			"GetFirstClient" },
    { GetFirstRegion,			"GetFirstRegion" },
    { GetFirstTuple,			"GetFirstTuple" },
    { GetNextClient,			"GetNextClient" },
    { GetNextRegion,			"GetNextRegion" },
    { GetNextTuple,			"GetNextTuple" },
    { GetStatus,			"GetStatus" },
    { GetTupleData,			"GetTupleData" },
    { MapMemPage,			"MapMemPage" },
    { ModifyConfiguration,		"ModifyConfiguration" },
    { ModifyWindow,			"ModifyWindow" },
    { OpenMemory,			"OpenMemory" },
    { ParseTuple,			"ParseTuple" },
    { ReadMemory,			"ReadMemory" },
    { RegisterClient,			"RegisterClient" },
    { RegisterEraseQueue,		"RegisterEraseQueue" },
    { RegisterMTD,			"RegisterMTD" },
    { ReleaseConfiguration,		"ReleaseConfiguration" },
    { ReleaseIO,			"ReleaseIO" },
    { ReleaseIRQ,			"ReleaseIRQ" },
    { ReleaseWindow,			"ReleaseWindow" },
    { RequestConfiguration,		"RequestConfiguration" },
    { RequestIO,			"RequestIO" },
    { RequestIRQ,			"RequestIRQ" },
    { RequestSocketMask,		"RequestSocketMask" },
    { RequestWindow,			"RequestWindow" },
    { ResetCard,			"ResetCard" },
    { SetEventMask,			"SetEventMask" },
    { ValidateCIS,			"ValidateCIS" },
    { WriteMemory,			"WriteMemory" },
    { BindDevice,			"BindDevice" },
    { BindMTD,				"BindMTD" },
    { ReportError,			"ReportError" },
    { SuspendCard,			"SuspendCard" },
    { ResumeCard,			"ResumeCard" },
    { EjectCard,			"EjectCard" },
    { InsertCard,			"InsertCard" },
    { ReplaceCIS,			"ReplaceCIS" }
};


int pcmcia_report_error(client_handle_t handle, error_info_t *err)
{
	int i;
	char *serv;

	if (CHECK_HANDLE(handle))
		printk(KERN_NOTICE);
	else {
		struct pcmcia_device *p_dev = handle_to_pdev(handle);
		printk(KERN_NOTICE "%s: ", p_dev->dev.bus_id);
	}

	for (i = 0; i < ARRAY_SIZE(service_table); i++)
		if (service_table[i].key == err->func)
			break;
	if (i < ARRAY_SIZE(service_table))
		serv = service_table[i].msg;
	else
		serv = "Unknown service number";

	for (i = 0; i < ARRAY_SIZE(error_table); i++)
		if (error_table[i].key == err->retcode)
			break;
	if (i < ARRAY_SIZE(error_table))
		printk("%s: %s\n", serv, error_table[i].msg);
	else
		printk("%s: Unknown error code %#x\n", serv, err->retcode);

	return CS_SUCCESS;
} /* report_error */
EXPORT_SYMBOL(pcmcia_report_error);

/* end of code which was in cs.c before */

/*======================================================================*/

void cs_error(client_handle_t handle, int func, int ret)
{
	error_info_t err = { func, ret };
	pcmcia_report_error(handle, &err);
}
EXPORT_SYMBOL(cs_error);

/*======================================================================*/

static struct pcmcia_driver * get_pcmcia_driver (dev_info_t *dev_info);
static struct pcmcia_bus_socket * get_socket_info_by_nr(unsigned int nr);

static void pcmcia_release_bus_socket(struct kref *refcount)
{
	struct pcmcia_bus_socket *s = container_of(refcount, struct pcmcia_bus_socket, refcount);
	pcmcia_put_socket(s->parent);
	kfree(s);
}

static void pcmcia_put_bus_socket(struct pcmcia_bus_socket *s)
{
	kref_put(&s->refcount, pcmcia_release_bus_socket);
}

static struct pcmcia_bus_socket *pcmcia_get_bus_socket(struct pcmcia_bus_socket *s)
{
	kref_get(&s->refcount);
	return (s);
}

/**
 * pcmcia_register_driver - register a PCMCIA driver with the bus core
 *
 * Registers a PCMCIA driver with the PCMCIA bus core.
 */
static int pcmcia_device_probe(struct device *dev);
static int pcmcia_device_remove(struct device * dev);

int pcmcia_register_driver(struct pcmcia_driver *driver)
{
	if (!driver)
		return -EINVAL;

	/* initialize common fields */
	driver->drv.bus = &pcmcia_bus_type;
	driver->drv.owner = driver->owner;
	driver->drv.probe = pcmcia_device_probe;
	driver->drv.remove = pcmcia_device_remove;

	return driver_register(&driver->drv);
}
EXPORT_SYMBOL(pcmcia_register_driver);

/**
 * pcmcia_unregister_driver - unregister a PCMCIA driver with the bus core
 */
void pcmcia_unregister_driver(struct pcmcia_driver *driver)
{
	driver_unregister(&driver->drv);
}
EXPORT_SYMBOL(pcmcia_unregister_driver);

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_pccard = NULL;

static int proc_read_drivers_callback(struct device_driver *driver, void *d)
{
	char **p = d;
	struct pcmcia_driver *p_drv = container_of(driver,
						   struct pcmcia_driver, drv);

	*p += sprintf(*p, "%-24.24s 1 %d\n", p_drv->drv.name,
#ifdef CONFIG_MODULE_UNLOAD
		      (p_drv->owner) ? module_refcount(p_drv->owner) : 1
#else
		      1
#endif
	);
	d = (void *) p;

	return 0;
}

static int proc_read_drivers(char *buf, char **start, off_t pos,
			     int count, int *eof, void *data)
{
	char *p = buf;

	bus_for_each_drv(&pcmcia_bus_type, NULL, 
			 (void *) &p, proc_read_drivers_callback);

	return (p - buf);
}
#endif

/* pcmcia_device handling */

static struct pcmcia_device * pcmcia_get_dev(struct pcmcia_device *p_dev)
{
	struct device *tmp_dev;
	tmp_dev = get_device(&p_dev->dev);
	if (!tmp_dev)
		return NULL;
	return to_pcmcia_dev(tmp_dev);
}

static void pcmcia_put_dev(struct pcmcia_device *p_dev)
{
	if (p_dev)
		put_device(&p_dev->dev);
}

static void pcmcia_release_dev(struct device *dev)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	ds_dbg(1, "releasing dev %p\n", p_dev);
	pcmcia_put_bus_socket(p_dev->socket->pcmcia);
	kfree(p_dev);
}


static int pcmcia_device_probe(struct device * dev)
{
	struct pcmcia_device *p_dev;
	struct pcmcia_driver *p_drv;
	int ret = 0;

	dev = get_device(dev);
	if (!dev)
		return -ENODEV;

	p_dev = to_pcmcia_dev(dev);
	p_drv = to_pcmcia_drv(dev->driver);

	if (!try_module_get(p_drv->owner)) {
		ret = -EINVAL;
		goto put_dev;
	}

	if (p_drv->attach) {
		p_dev->instance = p_drv->attach();
		if ((!p_dev->instance) || (p_dev->client.state & CLIENT_UNBOUND)) {
			printk(KERN_NOTICE "ds: unable to create instance "
			       "of '%s'!\n", p_drv->drv.name);
			ret = -EINVAL;
		}
	}

	if (ret)
		module_put(p_drv->owner);
 put_dev:
	if ((ret) || !(p_drv->attach))
		put_device(dev);
	return (ret);
}


static int pcmcia_device_remove(struct device * dev)
{
	struct pcmcia_device *p_dev;
	struct pcmcia_driver *p_drv;

	/* detach the "instance" */
	p_dev = to_pcmcia_dev(dev);
	p_drv = to_pcmcia_drv(dev->driver);

	if (p_drv) {
		if ((p_drv->detach) && (p_dev->instance)) {
			p_drv->detach(p_dev->instance);
			/* from pcmcia_probe_device */
			put_device(&p_dev->dev);
		}
		module_put(p_drv->owner);
	}

	return 0;
}



/*
 * pcmcia_device_query -- determine information about a pcmcia device
 */
static int pcmcia_device_query(struct pcmcia_device *p_dev)
{
	cistpl_manfid_t manf_id;
	cistpl_funcid_t func_id;
	cistpl_vers_1_t	vers1;
	unsigned int i;

	if (!pccard_read_tuple(p_dev->socket, p_dev->func,
			       CISTPL_MANFID, &manf_id)) {
		p_dev->manf_id = manf_id.manf;
		p_dev->card_id = manf_id.card;
		p_dev->has_manf_id = 1;
		p_dev->has_card_id = 1;
	}

	if (!pccard_read_tuple(p_dev->socket, p_dev->func,
			       CISTPL_FUNCID, &func_id)) {
		p_dev->func_id = func_id.func;
		p_dev->has_func_id = 1;
	} else {
		/* rule of thumb: cards with no FUNCID, but with
		 * common memory device geometry information, are
		 * probably memory cards (from pcmcia-cs) */
		cistpl_device_geo_t devgeo;
		if (!pccard_read_tuple(p_dev->socket, p_dev->func,
				      CISTPL_DEVICE_GEO, &devgeo)) {
			ds_dbg(0, "mem device geometry probably means "
			       "FUNCID_MEMORY\n");
			p_dev->func_id = CISTPL_FUNCID_MEMORY;
			p_dev->has_func_id = 1;
		}
	}

	if (!pccard_read_tuple(p_dev->socket, p_dev->func, CISTPL_VERS_1,
			       &vers1)) {
		for (i=0; i < vers1.ns; i++) {
			char *tmp;
			unsigned int length;

			tmp = vers1.str + vers1.ofs[i];

			length = strlen(tmp) + 1;
			if ((length < 3) || (length > 255))
				continue;

			p_dev->prod_id[i] = kmalloc(sizeof(char) * length,
						    GFP_KERNEL);
			if (!p_dev->prod_id[i])
				continue;

			p_dev->prod_id[i] = strncpy(p_dev->prod_id[i],
						    tmp, length);
		}
	}

	return 0;
}


/* device_add_lock is needed to avoid double registration by cardmgr and kernel.
 * Serializes pcmcia_device_add; will most likely be removed in future.
 *
 * While it has the caveat that adding new PCMCIA devices inside(!) device_register()
 * won't work, this doesn't matter much at the moment: the driver core doesn't
 * support it either.
 */
static DECLARE_MUTEX(device_add_lock);

static struct pcmcia_device * pcmcia_device_add(struct pcmcia_bus_socket *s, unsigned int function)
{
	struct pcmcia_device *p_dev;
	unsigned long flags;

	s = pcmcia_get_bus_socket(s);
	if (!s)
		return NULL;

	down(&device_add_lock);

	p_dev = kmalloc(sizeof(struct pcmcia_device), GFP_KERNEL);
	if (!p_dev)
		goto err_put;
	memset(p_dev, 0, sizeof(struct pcmcia_device));

	p_dev->socket = s->parent;
	p_dev->device_no = (s->device_count++);
	p_dev->func   = function;

	p_dev->dev.bus = &pcmcia_bus_type;
	p_dev->dev.parent = s->parent->dev.dev;
	p_dev->dev.release = pcmcia_release_dev;
	sprintf (p_dev->dev.bus_id, "%d.%d", p_dev->socket->sock, p_dev->device_no);

	/* compat */
	p_dev->client.client_magic = CLIENT_MAGIC;
	p_dev->client.Socket = s->parent;
	p_dev->client.Function = function;
	p_dev->client.state = CLIENT_UNBOUND;

	/* Add to the list in pcmcia_bus_socket */
	spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
	list_add_tail(&p_dev->socket_device_list, &s->devices_list);
	spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);

	if (device_register(&p_dev->dev)) {
		spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
		list_del(&p_dev->socket_device_list);
		spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);

		goto err_free;
       }

	up(&device_add_lock);

	return p_dev;

 err_free:
	kfree(p_dev);
	s->device_count--;
 err_put:
	up(&device_add_lock);
	pcmcia_put_bus_socket(s);

	return NULL;
}


static int pcmcia_card_add(struct pcmcia_socket *s)
{
	cisinfo_t cisinfo;
	cistpl_longlink_mfc_t mfc;
	unsigned int no_funcs, i;
	int ret = 0;

	if (!(s->resource_setup_done))
		return -EAGAIN; /* try again, but later... */

	pcmcia_validate_mem(s);
	ret = pccard_validate_cis(s, BIND_FN_ALL, &cisinfo);
	if (ret || !cisinfo.Chains) {
		ds_dbg(0, "invalid CIS or invalid resources\n");
		return -ENODEV;
	}

	if (!pccard_read_tuple(s, BIND_FN_ALL, CISTPL_LONGLINK_MFC, &mfc))
		no_funcs = mfc.nfn;
	else
		no_funcs = 1;

	/* this doesn't handle multifunction devices on one pcmcia function
	 * yet. */
	for (i=0; i < no_funcs; i++)
		pcmcia_device_add(s->pcmcia, i);

	return (ret);
}


static int pcmcia_bus_match(struct device * dev, struct device_driver * drv) {
	struct pcmcia_device * p_dev = to_pcmcia_dev(dev);
	struct pcmcia_driver * p_drv = to_pcmcia_drv(drv);

	/* matching by cardmgr */
	if (p_dev->cardmgr == p_drv)
		return 1;

	return 0;
}

/************************ per-device sysfs output ***************************/

#define pcmcia_device_attr(field, test, format)				\
static ssize_t field##_show (struct device *dev, char *buf)		\
{									\
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);		\
	return p_dev->test ? sprintf (buf, format, p_dev->field) : -ENODEV; \
}

#define pcmcia_device_stringattr(name, field)					\
static ssize_t name##_show (struct device *dev, char *buf)		\
{									\
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);		\
	return p_dev->field ? sprintf (buf, "%s\n", p_dev->field) : -ENODEV; \
}

pcmcia_device_attr(func, socket, "0x%02x\n");
pcmcia_device_attr(func_id, has_func_id, "0x%02x\n");
pcmcia_device_attr(manf_id, has_manf_id, "0x%04x\n");
pcmcia_device_attr(card_id, has_card_id, "0x%04x\n");
pcmcia_device_stringattr(prod_id1, prod_id[0]);
pcmcia_device_stringattr(prod_id2, prod_id[1]);
pcmcia_device_stringattr(prod_id3, prod_id[2]);
pcmcia_device_stringattr(prod_id4, prod_id[3]);

static struct device_attribute pcmcia_dev_attrs[] = {
	__ATTR(function, 0444, func_show, NULL),
	__ATTR_RO(func_id),
	__ATTR_RO(manf_id),
	__ATTR_RO(card_id),
	__ATTR_RO(prod_id1),
	__ATTR_RO(prod_id2),
	__ATTR_RO(prod_id3),
	__ATTR_RO(prod_id4),
	__ATTR_NULL,
};


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

static void handle_event(struct pcmcia_bus_socket *s, event_t event)
{
    user_info_t *user;
    for (user = s->user; user; user = user->next)
	queue_event(user, event);
    wake_up_interruptible(&s->queue);
}


/*======================================================================

    The card status event handler.
    
======================================================================*/

struct send_event_data {
	struct pcmcia_socket *skt;
	event_t event;
	int priority;
};

static int send_event_callback(struct device *dev, void * _data)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	struct send_event_data *data = _data;

	/* we get called for all sockets, but may only pass the event
	 * for drivers _on the affected socket_ */
	if (p_dev->socket != data->skt)
		return 0;

	if (p_dev->client.state & (CLIENT_UNBOUND|CLIENT_STALE))
		return 0;

	if (p_dev->client.EventMask & data->event)
		return EVENT(&p_dev->client, data->event, data->priority);

	return 0;
}

static int send_event(struct pcmcia_socket *s, event_t event, int priority)
{
	int ret = 0;
	struct send_event_data private;
	struct pcmcia_bus_socket *skt = pcmcia_get_bus_socket(s->pcmcia);

	if (!skt)
		return 0;

	private.skt = s;
	private.event = event;
	private.priority = priority;

	ret = bus_for_each_dev(&pcmcia_bus_type, NULL, &private, send_event_callback);

	pcmcia_put_bus_socket(skt);
	return ret;
} /* send_event */


/* Normally, the event is passed to individual drivers after
 * informing userspace. Only for CS_EVENT_CARD_REMOVAL this
 * is inversed to maintain historic compatibility.
 */

static int ds_event(struct pcmcia_socket *skt, event_t event, int priority)
{
	struct pcmcia_bus_socket *s = skt->pcmcia;
	int ret = 0;

	ds_dbg(1, "ds_event(0x%06x, %d, 0x%p)\n",
	       event, priority, s);
    
	switch (event) {

	case CS_EVENT_CARD_REMOVAL:
		s->state &= ~DS_SOCKET_PRESENT;
	    	send_event(skt, event, priority);
		unbind_request(s);
		handle_event(s, event);
		break;
	
	case CS_EVENT_CARD_INSERTION:
		s->state |= DS_SOCKET_PRESENT;
		pcmcia_card_add(skt);
		handle_event(s, event);
		break;

	case CS_EVENT_EJECTION_REQUEST:
		ret = send_event(skt, event, priority);
		break;

	default:
		handle_event(s, event);
		send_event(skt, event, priority);
		break;
    }

    return 0;
} /* ds_event */


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

static int bind_request(struct pcmcia_bus_socket *s, bind_info_t *bind_info)
{
	struct pcmcia_driver *p_drv;
	struct pcmcia_device *p_dev;
	int ret = 0;
	unsigned long flags;

	s = pcmcia_get_bus_socket(s);
	if (!s)
		return -EINVAL;

	ds_dbg(2, "bind_request(%d, '%s')\n", s->parent->sock,
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
					bind_info->instance = p_dev->instance;
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

	pcmcia_device_query(p_dev);

	/*
	 * Prevent this racing with a card insertion.
	 */
	down(&s->parent->skt_sem);
	bus_rescan_devices(&pcmcia_bus_type);
	up(&s->parent->skt_sem);

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
	pcmcia_put_bus_socket(s);

	return (ret);
} /* bind_request */


int pcmcia_register_client(client_handle_t *handle, client_reg_t *req)
{
	client_t *client = NULL;
	struct pcmcia_socket *s;
	struct pcmcia_bus_socket *skt = NULL;
	struct pcmcia_device *p_dev = NULL;

	/* Look for unbound client with matching dev_info */
	down_read(&pcmcia_socket_list_rwsem);
	list_for_each_entry(s, &pcmcia_socket_list, socket_list) {
		unsigned long flags;

		if (s->state & SOCKET_CARDBUS)
			continue;

		skt = s->pcmcia;
		if (!skt)
			continue;
		skt = pcmcia_get_bus_socket(skt);
		if (!skt)
			continue;
		spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
		list_for_each_entry(p_dev, &skt->devices_list, socket_device_list) {
			struct pcmcia_driver *p_drv;
			p_dev = pcmcia_get_dev(p_dev);
			if (!p_dev)
				continue;
			if (!(p_dev->client.state & CLIENT_UNBOUND) ||
			    (!p_dev->dev.driver)) {
				pcmcia_put_dev(p_dev);
				continue;
			}
			p_drv = to_pcmcia_drv(p_dev->dev.driver);
			if (!strncmp(p_drv->drv.name, (char *)req->dev_info, DEV_NAME_LEN)) {
				client = &p_dev->client;
				spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
				goto found;
			}
			pcmcia_put_dev(p_dev);
		}
		spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
		pcmcia_put_bus_socket(skt);
	}
 found:
	up_read(&pcmcia_socket_list_rwsem);
	if (!p_dev || !client)
		return -ENODEV;

	pcmcia_put_bus_socket(skt); /* safe, as we already hold a reference from bind_device */

	*handle = client;
	client->state &= ~CLIENT_UNBOUND;
	client->Socket = s;
	client->EventMask = req->EventMask;
	client->event_handler = req->event_handler;
	client->event_callback_args = req->event_callback_args;
	client->event_callback_args.client_handle = client;

	if (s->state & SOCKET_CARDBUS)
		client->state |= CLIENT_CARDBUS;

	if ((!(s->state & SOCKET_CARDBUS)) && (s->functions == 0) &&
	    (client->Function != BIND_FN_ALL)) {
		cistpl_longlink_mfc_t mfc;
		if (pccard_read_tuple(s, client->Function, CISTPL_LONGLINK_MFC, &mfc)
		    == CS_SUCCESS)
			s->functions = mfc.nfn;
		else
			s->functions = 1;
		s->config = kmalloc(sizeof(config_t) * s->functions,
				    GFP_KERNEL);
		if (!s->config)
			goto out_no_resource;
		memset(s->config, 0, sizeof(config_t) * s->functions);
	}

	ds_dbg(1, "register_client(): client 0x%p, dev %s\n",
	       client, p_dev->dev.bus_id);
	if (client->EventMask & CS_EVENT_REGISTRATION_COMPLETE)
		EVENT(client, CS_EVENT_REGISTRATION_COMPLETE, CS_EVENT_PRI_LOW);

	if ((s->state & (SOCKET_PRESENT|SOCKET_CARDBUS)) == SOCKET_PRESENT) {
		if (client->EventMask & CS_EVENT_CARD_INSERTION)
			EVENT(client, CS_EVENT_CARD_INSERTION, CS_EVENT_PRI_LOW);
	}

	return CS_SUCCESS;

 out_no_resource:
	pcmcia_put_dev(p_dev);
	return CS_OUT_OF_RESOURCE;
} /* register_client */
EXPORT_SYMBOL(pcmcia_register_client);


/*====================================================================*/

extern struct pci_bus *pcmcia_lookup_bus(struct pcmcia_socket *s);

static int get_device_info(struct pcmcia_bus_socket *s, bind_info_t *bind_info, int first)
{
	dev_node_t *node;
	struct pcmcia_device *p_dev;
	unsigned long flags;
	int ret = 0;

#ifdef CONFIG_CARDBUS
	/*
	 * Some unbelievably ugly code to associate the PCI cardbus
	 * device and its driver with the PCMCIA "bind" information.
	 */
	{
		struct pci_bus *bus;

		bus = pcmcia_lookup_bus(s->parent);
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

	if ((!p_dev->instance) ||
	    (p_dev->instance->state & DEV_CONFIG_PENDING)) {
		ret = -EAGAIN;
		goto err_put;
	}

	if (first)
		node = p_dev->instance->dev;
	else
		for (node = p_dev->instance->dev; node; node = node->next)
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

/*====================================================================*/

/* unbind _all_ devices attached to a given pcmcia_bus_socket. The
 * drivers have been called with EVENT_CARD_REMOVAL before.
 */
static int unbind_request(struct pcmcia_bus_socket *s)
{
	struct pcmcia_device	*p_dev;
	unsigned long		flags;

	ds_dbg(2, "unbind_request(%d)\n", s->parent->sock);

	s->device_count = 0;

	for (;;) {
		/* unregister all pcmcia_devices registered with this socket*/
		spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
		if (list_empty(&s->devices_list)) {
			spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
 			return 0;
		}
		p_dev = list_entry((&s->devices_list)->next, struct pcmcia_device, socket_device_list);
		list_del(&p_dev->socket_device_list);
		p_dev->client.state |= CLIENT_STALE;
		spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);

		device_unregister(&p_dev->dev);
	}

	return 0;
} /* unbind_request */

int pcmcia_deregister_client(client_handle_t handle)
{
	struct pcmcia_socket *s;
	int i;
	struct pcmcia_device *p_dev = handle_to_pdev(handle);

	if (CHECK_HANDLE(handle))
		return CS_BAD_HANDLE;

	s = SOCKET(handle);
	ds_dbg(1, "deregister_client(%p)\n", handle);

	if (handle->state & (CLIENT_IRQ_REQ|CLIENT_IO_REQ|CLIENT_CONFIG_LOCKED))
		goto warn_out;
	for (i = 0; i < MAX_WIN; i++)
		if (handle->state & CLIENT_WIN_REQ(i))
			goto warn_out;

	if (handle->state & CLIENT_STALE) {
		handle->client_magic = 0;
		handle->state &= ~CLIENT_STALE;
		pcmcia_put_dev(p_dev);
	} else {
		handle->state = CLIENT_UNBOUND;
		handle->event_handler = NULL;
	}

	return CS_SUCCESS;
 warn_out:
	printk(KERN_WARNING "ds: deregister_client was called too early.\n");
	return CS_IN_USE;
} /* deregister_client */
EXPORT_SYMBOL(pcmcia_deregister_client);


/*======================================================================

    The user-mode PC Card device interface

======================================================================*/

static int ds_open(struct inode *inode, struct file *file)
{
    socket_t i = iminor(inode);
    struct pcmcia_bus_socket *s;
    user_info_t *user;

    ds_dbg(0, "ds_open(socket %d)\n", i);

    s = get_socket_info_by_nr(i);
    if (!s)
	    return -ENODEV;
    s = pcmcia_get_bus_socket(s);
    if (!s)
	    return -ENODEV;

    if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
	    if (s->state & DS_SOCKET_BUSY) {
		    pcmcia_put_bus_socket(s);
		    return -EBUSY;
	    }
	else
	    s->state |= DS_SOCKET_BUSY;
    }
    
    user = kmalloc(sizeof(user_info_t), GFP_KERNEL);
    if (!user) {
	    pcmcia_put_bus_socket(s);
	    return -ENOMEM;
    }
    user->event_tail = user->event_head = 0;
    user->next = s->user;
    user->user_magic = USER_MAGIC;
    user->socket = s;
    s->user = user;
    file->private_data = user;
    
    if (s->state & DS_SOCKET_PRESENT)
	queue_event(user, CS_EVENT_CARD_INSERTION);
    return 0;
} /* ds_open */

/*====================================================================*/

static int ds_release(struct inode *inode, struct file *file)
{
    struct pcmcia_bus_socket *s;
    user_info_t *user, **link;

    ds_dbg(0, "ds_release(socket %d)\n", iminor(inode));

    user = file->private_data;
    if (CHECK_USER(user))
	goto out;

    s = user->socket;

    /* Unlink user data structure */
    if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
	s->state &= ~DS_SOCKET_BUSY;
    }
    file->private_data = NULL;
    for (link = &s->user; *link; link = &(*link)->next)
	if (*link == user) break;
    if (link == NULL)
	goto out;
    *link = user->next;
    user->user_magic = 0;
    kfree(user);
    pcmcia_put_bus_socket(s);
out:
    return 0;
} /* ds_release */

/*====================================================================*/

static ssize_t ds_read(struct file *file, char __user *buf,
		       size_t count, loff_t *ppos)
{
    struct pcmcia_bus_socket *s;
    user_info_t *user;
    int ret;

    ds_dbg(2, "ds_read(socket %d)\n", iminor(file->f_dentry->d_inode));
    
    if (count < 4)
	return -EINVAL;

    user = file->private_data;
    if (CHECK_USER(user))
	return -EIO;
    
    s = user->socket;
    if (s->state & DS_SOCKET_DEAD)
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
    ds_dbg(2, "ds_write(socket %d)\n", iminor(file->f_dentry->d_inode));

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
    struct pcmcia_bus_socket *s;
    user_info_t *user;

    ds_dbg(2, "ds_poll(socket %d)\n", iminor(file->f_dentry->d_inode));
    
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

extern int pcmcia_adjust_resource_info(adjust_t *adj);

static int ds_ioctl(struct inode * inode, struct file * file,
		    u_int cmd, u_long arg)
{
    struct pcmcia_bus_socket *s;
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
    if (s->state & DS_SOCKET_DEAD)
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
    
    if (cmd & IOC_IN) __copy_from_user((char *)buf, uarg, size);
    
    switch (cmd) {
    case DS_ADJUST_RESOURCE_INFO:
	ret = pcmcia_adjust_resource_info(&buf->adjust);
	break;
    case DS_GET_CARD_SERVICES_INFO:
	ret = pcmcia_get_card_services_info(&buf->servinfo);
	break;
    case DS_GET_CONFIGURATION_INFO:
	if (buf->config.Function &&
	   (buf->config.Function >= s->parent->functions))
	    ret = CS_BAD_ARGS;
	else
	    ret = pccard_get_configuration_info(s->parent,
			buf->config.Function, &buf->config);
	break;
    case DS_GET_FIRST_TUPLE:
	down(&s->parent->skt_sem);
	pcmcia_validate_mem(s->parent);
	up(&s->parent->skt_sem);
	ret = pccard_get_first_tuple(s->parent, BIND_FN_ALL, &buf->tuple);
	break;
    case DS_GET_NEXT_TUPLE:
	ret = pccard_get_next_tuple(s->parent, BIND_FN_ALL, &buf->tuple);
	break;
    case DS_GET_TUPLE_DATA:
	buf->tuple.TupleData = buf->tuple_parse.data;
	buf->tuple.TupleDataMax = sizeof(buf->tuple_parse.data);
	ret = pccard_get_tuple_data(s->parent, &buf->tuple);
	break;
    case DS_PARSE_TUPLE:
	buf->tuple.TupleData = buf->tuple_parse.data;
	ret = pccard_parse_tuple(&buf->tuple, &buf->tuple_parse.parse);
	break;
    case DS_RESET_CARD:
	ret = pccard_reset_card(s->parent);
	break;
    case DS_GET_STATUS:
	if (buf->status.Function &&
	   (buf->status.Function >= s->parent->functions))
	    ret = CS_BAD_ARGS;
	else
	ret = pccard_get_status(s->parent, buf->status.Function, &buf->status);
	break;
    case DS_VALIDATE_CIS:
	down(&s->parent->skt_sem);
	pcmcia_validate_mem(s->parent);
	up(&s->parent->skt_sem);
	ret = pccard_validate_cis(s->parent, BIND_FN_ALL, &buf->cisinfo);
	break;
    case DS_SUSPEND_CARD:
	ret = pcmcia_suspend_card(s->parent);
	break;
    case DS_RESUME_CARD:
	ret = pcmcia_resume_card(s->parent);
	break;
    case DS_EJECT_CARD:
	err = pcmcia_eject_card(s->parent);
	break;
    case DS_INSERT_CARD:
	err = pcmcia_insert_card(s->parent);
	break;
    case DS_ACCESS_CONFIGURATION_REGISTER:
	if ((buf->conf_reg.Action == CS_WRITE) && !capable(CAP_SYS_ADMIN)) {
	    err = -EPERM;
	    goto free_out;
	}
	if (buf->conf_reg.Function &&
	   (buf->conf_reg.Function >= s->parent->functions))
	    ret = CS_BAD_ARGS;
	else
	    ret = pccard_access_configuration_register(s->parent,
			buf->conf_reg.Function, &buf->conf_reg);
	break;
    case DS_GET_FIRST_REGION:
    case DS_GET_NEXT_REGION:
    case DS_BIND_MTD:
	if (!capable(CAP_SYS_ADMIN)) {
		err = -EPERM;
		goto free_out;
	} else {
		static int printed = 0;
		if (!printed) {
			printk(KERN_WARNING "2.6. kernels use pcmciamtd instead of memory_cs.c and do not require special\n");
			printk(KERN_WARNING "MTD handling any more.\n");
			printed++;
		}
	}
	err = -EINVAL;
	goto free_out;
	break;
    case DS_GET_FIRST_WINDOW:
	ret = pcmcia_get_window(s->parent, &buf->win_info.handle, 0,
			&buf->win_info.window);
	break;
    case DS_GET_NEXT_WINDOW:
	ret = pcmcia_get_window(s->parent, &buf->win_info.handle,
			buf->win_info.handle->index + 1, &buf->win_info.window);
	break;
    case DS_GET_MEM_PAGE:
	ret = pcmcia_get_mem_page(buf->win_info.handle,
			   &buf->win_info.map);
	break;
    case DS_REPLACE_CIS:
	ret = pcmcia_replace_cis(s->parent, &buf->cisdump);
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
    
    if ((err == 0) && (ret != CS_SUCCESS)) {
	ds_dbg(2, "ds_ioctl: ret = %d\n", ret);
	switch (ret) {
	case CS_BAD_SOCKET: case CS_NO_CARD:
	    err = -ENODEV; break;
	case CS_BAD_ARGS: case CS_BAD_ATTRIBUTE: case CS_BAD_IRQ:
	case CS_BAD_TUPLE:
	    err = -EINVAL; break;
	case CS_IN_USE:
	    err = -EBUSY; break;
	case CS_OUT_OF_RESOURCE:
	    err = -ENOSPC; break;
	case CS_NO_MORE_ITEMS:
	    err = -ENODATA; break;
	case CS_UNSUPPORTED_FUNCTION:
	    err = -ENOSYS; break;
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

static struct file_operations ds_fops = {
	.owner		= THIS_MODULE,
	.open		= ds_open,
	.release	= ds_release,
	.ioctl		= ds_ioctl,
	.read		= ds_read,
	.write		= ds_write,
	.poll		= ds_poll,
};

static int __devinit pcmcia_bus_add_socket(struct class_device *class_dev)
{
	struct pcmcia_socket *socket = class_get_devdata(class_dev);
	struct pcmcia_bus_socket *s;
	int ret;

	s = kmalloc(sizeof(struct pcmcia_bus_socket), GFP_KERNEL);
	if(!s)
		return -ENOMEM;
	memset(s, 0, sizeof(struct pcmcia_bus_socket));

	/* get reference to parent socket */
	s->parent = pcmcia_get_socket(socket);
	if (!s->parent) {
		printk(KERN_ERR "PCMCIA obtaining reference to socket %p failed\n", socket);
		kfree (s);
		return -ENODEV;
	}

	kref_init(&s->refcount);
    
	/*
	 * Ugly. But we want to wait for the socket threads to have started up.
	 * We really should let the drivers themselves drive some of this..
	 */
	msleep(250);

	init_waitqueue_head(&s->queue);
	INIT_LIST_HEAD(&s->devices_list);

	/* Set up hotline to Card Services */
	s->callback.owner = THIS_MODULE;
	s->callback.event = &ds_event;
	s->callback.resources_done = &pcmcia_card_add;
	socket->pcmcia = s;

	ret = pccard_register_pcmcia(socket, &s->callback);
	if (ret) {
		printk(KERN_ERR "PCMCIA registration PCCard core failed for socket %p\n", socket);
		pcmcia_put_bus_socket(s);
		socket->pcmcia = NULL;
		return (ret);
	}

	return 0;
}


static void pcmcia_bus_remove_socket(struct class_device *class_dev)
{
	struct pcmcia_socket *socket = class_get_devdata(class_dev);

	if (!socket || !socket->pcmcia)
		return;

	pccard_register_pcmcia(socket, NULL);

	socket->pcmcia->state |= DS_SOCKET_DEAD;
	pcmcia_put_bus_socket(socket->pcmcia);
	socket->pcmcia = NULL;

	return;
}


/* the pcmcia_bus_interface is used to handle pcmcia socket devices */
static struct class_interface pcmcia_bus_interface = {
	.class = &pcmcia_socket_class,
	.add = &pcmcia_bus_add_socket,
	.remove = &pcmcia_bus_remove_socket,
};


struct bus_type pcmcia_bus_type = {
	.name = "pcmcia",
	.match = pcmcia_bus_match,
	.dev_attrs = pcmcia_dev_attrs,
};
EXPORT_SYMBOL(pcmcia_bus_type);


static int __init init_pcmcia_bus(void)
{
	int i;

	spin_lock_init(&pcmcia_dev_list_lock);

	bus_register(&pcmcia_bus_type);
	class_interface_register(&pcmcia_bus_interface);

	/* Set up character device for user mode clients */
	i = register_chrdev(0, "pcmcia", &ds_fops);
	if (i < 0)
		printk(KERN_NOTICE "unable to find a free device # for "
		       "Driver Services (error=%d)\n", i);
	else
		major_dev = i;

#ifdef CONFIG_PROC_FS
	proc_pccard = proc_mkdir("pccard", proc_bus);
	if (proc_pccard)
		create_proc_read_entry("drivers",0,proc_pccard,proc_read_drivers,NULL);
#endif

	return 0;
}
fs_initcall(init_pcmcia_bus); /* one level after subsys_initcall so that 
			       * pcmcia_socket_class is already registered */


static void __exit exit_pcmcia_bus(void)
{
	class_interface_unregister(&pcmcia_bus_interface);

#ifdef CONFIG_PROC_FS
	if (proc_pccard) {
		remove_proc_entry("drivers", proc_pccard);
		remove_proc_entry("pccard", proc_bus);
	}
#endif
	if (major_dev != -1)
		unregister_chrdev(major_dev, "pcmcia");

	bus_unregister(&pcmcia_bus_type);
}
module_exit(exit_pcmcia_bus);



/* helpers for backwards-compatible functions */

static struct pcmcia_bus_socket * get_socket_info_by_nr(unsigned int nr)
{
	struct pcmcia_socket * s = pcmcia_get_socket_by_nr(nr);
	if (s && s->pcmcia)
		return s->pcmcia;
	else
		return NULL;
}

/* backwards-compatible accessing of driver --- by name! */

static struct pcmcia_driver * get_pcmcia_driver (dev_info_t *dev_info)
{
	struct device_driver *drv;
	struct pcmcia_driver *p_drv;

	drv = driver_find((char *) dev_info, &pcmcia_bus_type);
	if (!drv)
		return NULL;

	p_drv = container_of(drv, struct pcmcia_driver, drv);

	return (p_drv);
}

MODULE_ALIAS("ds");
