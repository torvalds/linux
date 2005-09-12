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
 * (C) 2003 - 2005	Dominik Brodowski
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/crc32.h>
#include <linux/firmware.h>

#define IN_CARD_SERVICES
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/ss.h>

#include "cs_internal.h"
#include "ds_internal.h"

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

spinlock_t pcmcia_dev_list_lock;

static int unbind_request(struct pcmcia_socket *s);

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


static int pcmcia_report_error(struct pcmcia_device *p_dev, error_info_t *err)
{
	int i;
	char *serv;

	if (!p_dev)
		printk(KERN_NOTICE);
	else
		printk(KERN_NOTICE "%s: ", p_dev->dev.bus_id);

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

/* end of code which was in cs.c before */

/*======================================================================*/

void cs_error(struct pcmcia_device *p_dev, int func, int ret)
{
	error_info_t err = { func, ret };
	pcmcia_report_error(p_dev, &err);
}
EXPORT_SYMBOL(cs_error);


static void pcmcia_check_driver(struct pcmcia_driver *p_drv)
{
	struct pcmcia_device_id *did = p_drv->id_table;
	unsigned int i;
	u32 hash;

	if (!p_drv->attach || !p_drv->event || !p_drv->detach)
		printk(KERN_DEBUG "pcmcia: %s lacks a requisite callback "
		       "function\n", p_drv->drv.name);

	while (did && did->match_flags) {
		for (i=0; i<4; i++) {
			if (!did->prod_id[i])
				continue;

			hash = crc32(0, did->prod_id[i], strlen(did->prod_id[i]));
			if (hash == did->prod_id_hash[i])
				continue;

			printk(KERN_DEBUG "pcmcia: %s: invalid hash for "
			       "product string \"%s\": is 0x%x, should "
			       "be 0x%x\n", p_drv->drv.name, did->prod_id[i],
			       did->prod_id_hash[i], hash);
			printk(KERN_DEBUG "pcmcia: see "
				"Documentation/pcmcia/devicetable.txt for "
				"details\n");
		}
		did++;
	}

	return;
}


#ifdef CONFIG_PCMCIA_LOAD_CIS

/**
 * pcmcia_load_firmware - load CIS from userspace if device-provided is broken
 * @dev - the pcmcia device which needs a CIS override
 * @filename - requested filename in /lib/firmware/cis/
 *
 * This uses the in-kernel firmware loading mechanism to use a "fake CIS" if
 * the one provided by the card is broken. The firmware files reside in
 * /lib/firmware/cis/ in userspace.
 */
static int pcmcia_load_firmware(struct pcmcia_device *dev, char * filename)
{
	struct pcmcia_socket *s = dev->socket;
	const struct firmware *fw;
	char path[20];
	int ret=-ENOMEM;
	cisdump_t *cis;

	if (!filename)
		return -EINVAL;

	ds_dbg(1, "trying to load firmware %s\n", filename);

	if (strlen(filename) > 14)
		return -EINVAL;

	snprintf(path, 20, "%s", filename);

	if (request_firmware(&fw, path, &dev->dev) == 0) {
		if (fw->size >= CISTPL_MAX_CIS_SIZE)
			goto release;

		cis = kmalloc(sizeof(cisdump_t), GFP_KERNEL);
		if (!cis)
			goto release;

		memset(cis, 0, sizeof(cisdump_t));

		cis->Length = fw->size + 1;
		memcpy(cis->Data, fw->data, fw->size);

		if (!pcmcia_replace_cis(s, cis))
			ret = 0;
	}
 release:
	release_firmware(fw);

	return (ret);
}

#else /* !CONFIG_PCMCIA_LOAD_CIS */

static inline int pcmcia_load_firmware(struct pcmcia_device *dev, char * filename)
{
	return -ENODEV;
}

#endif


/*======================================================================*/


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

	pcmcia_check_driver(driver);

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


/* pcmcia_device handling */

struct pcmcia_device * pcmcia_get_dev(struct pcmcia_device *p_dev)
{
	struct device *tmp_dev;
	tmp_dev = get_device(&p_dev->dev);
	if (!tmp_dev)
		return NULL;
	return to_pcmcia_dev(tmp_dev);
}

void pcmcia_put_dev(struct pcmcia_device *p_dev)
{
	if (p_dev)
		put_device(&p_dev->dev);
}

static void pcmcia_release_dev(struct device *dev)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	ds_dbg(1, "releasing dev %p\n", p_dev);
	pcmcia_put_socket(p_dev->socket);
	kfree(p_dev->devname);
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
		if ((!p_dev->instance) || (p_dev->state & CLIENT_UNBOUND)) {
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
	cistpl_vers_1_t	*vers1;
	unsigned int i;

	vers1 = kmalloc(sizeof(*vers1), GFP_KERNEL);
	if (!vers1)
		return -ENOMEM;

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
		cistpl_device_geo_t *devgeo;

		devgeo = kmalloc(sizeof(*devgeo), GFP_KERNEL);
		if (!devgeo) {
			kfree(vers1);
			return -ENOMEM;
		}
		if (!pccard_read_tuple(p_dev->socket, p_dev->func,
				      CISTPL_DEVICE_GEO, devgeo)) {
			ds_dbg(0, "mem device geometry probably means "
			       "FUNCID_MEMORY\n");
			p_dev->func_id = CISTPL_FUNCID_MEMORY;
			p_dev->has_func_id = 1;
		}
		kfree(devgeo);
	}

	if (!pccard_read_tuple(p_dev->socket, p_dev->func, CISTPL_VERS_1,
			       vers1)) {
		for (i=0; i < vers1->ns; i++) {
			char *tmp;
			unsigned int length;

			tmp = vers1->str + vers1->ofs[i];

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

	kfree(vers1);
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

struct pcmcia_device * pcmcia_device_add(struct pcmcia_socket *s, unsigned int function)
{
	struct pcmcia_device *p_dev;
	unsigned long flags;
	int bus_id_len;

	s = pcmcia_get_socket(s);
	if (!s)
		return NULL;

	down(&device_add_lock);

	/* max of 2 devices per card */
	if (s->device_count == 2)
		goto err_put;

	p_dev = kmalloc(sizeof(struct pcmcia_device), GFP_KERNEL);
	if (!p_dev)
		goto err_put;
	memset(p_dev, 0, sizeof(struct pcmcia_device));

	p_dev->socket = s;
	p_dev->device_no = (s->device_count++);
	p_dev->func   = function;

	p_dev->dev.bus = &pcmcia_bus_type;
	p_dev->dev.parent = s->dev.dev;
	p_dev->dev.release = pcmcia_release_dev;
	bus_id_len = sprintf (p_dev->dev.bus_id, "%d.%d", p_dev->socket->sock, p_dev->device_no);

	p_dev->devname = kmalloc(6 + bus_id_len + 1, GFP_KERNEL);
	if (!p_dev->devname)
		goto err_free;
	sprintf (p_dev->devname, "pcmcia%s", p_dev->dev.bus_id);

	/* compat */
	p_dev->state = CLIENT_UNBOUND;

	/* Add to the list in pcmcia_bus_socket */
	spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
	list_add_tail(&p_dev->socket_device_list, &s->devices_list);
	spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);

	pcmcia_device_query(p_dev);

	if (device_register(&p_dev->dev)) {
		spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
		list_del(&p_dev->socket_device_list);
		spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);

		goto err_free;
       }

	up(&device_add_lock);

	return p_dev;

 err_free:
	kfree(p_dev->devname);
	kfree(p_dev);
	s->device_count--;
 err_put:
	up(&device_add_lock);
	pcmcia_put_socket(s);

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

	for (i=0; i < no_funcs; i++)
		pcmcia_device_add(s, i);

	return (ret);
}


static void pcmcia_delayed_add_pseudo_device(void *data)
{
	struct pcmcia_socket *s = data;
	pcmcia_device_add(s, 0);
	s->pcmcia_state.device_add_pending = 0;
}

static inline void pcmcia_add_pseudo_device(struct pcmcia_socket *s)
{
	if (!s->pcmcia_state.device_add_pending) {
		s->pcmcia_state.device_add_pending = 1;
		schedule_work(&s->device_add);
	}
	return;
}

static int pcmcia_requery(struct device *dev, void * _data)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	if (!p_dev->dev.driver)
		pcmcia_device_query(p_dev);

	return 0;
}

static void pcmcia_bus_rescan(struct pcmcia_socket *skt)
{
	int no_devices=0;
	unsigned long flags;

	/* must be called with skt_sem held */
	spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
	if (list_empty(&skt->devices_list))
		no_devices=1;
	spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);

	/* if no devices were added for this socket yet because of
	 * missing resource information or other trouble, we need to
	 * do this now. */
	if (no_devices) {
		int ret = pcmcia_card_add(skt);
		if (ret)
			return;
	}

	/* some device information might have changed because of a CIS
	 * update or because we can finally read it correctly... so
	 * determine it again, overwriting old values if necessary. */
	bus_for_each_dev(&pcmcia_bus_type, NULL, NULL, pcmcia_requery);

	/* we re-scan all devices, not just the ones connected to this
	 * socket. This does not matter, though. */
	bus_rescan_devices(&pcmcia_bus_type);
}

static inline int pcmcia_devmatch(struct pcmcia_device *dev,
				  struct pcmcia_device_id *did)
{
	if (did->match_flags & PCMCIA_DEV_ID_MATCH_MANF_ID) {
		if ((!dev->has_manf_id) || (dev->manf_id != did->manf_id))
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_CARD_ID) {
		if ((!dev->has_card_id) || (dev->card_id != did->card_id))
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_FUNCTION) {
		if (dev->func != did->function)
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID1) {
		if (!dev->prod_id[0])
			return 0;
		if (strcmp(did->prod_id[0], dev->prod_id[0]))
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID2) {
		if (!dev->prod_id[1])
			return 0;
		if (strcmp(did->prod_id[1], dev->prod_id[1]))
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID3) {
		if (!dev->prod_id[2])
			return 0;
		if (strcmp(did->prod_id[2], dev->prod_id[2]))
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_PROD_ID4) {
		if (!dev->prod_id[3])
			return 0;
		if (strcmp(did->prod_id[3], dev->prod_id[3]))
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_DEVICE_NO) {
		/* handle pseudo multifunction devices:
		 * there are at most two pseudo multifunction devices.
		 * if we're matching against the first, schedule a
		 * call which will then check whether there are two
		 * pseudo devices, and if not, add the second one.
		 */
		if (dev->device_no == 0)
			pcmcia_add_pseudo_device(dev->socket);

		if (dev->device_no != did->device_no)
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_FUNC_ID) {
		if ((!dev->has_func_id) || (dev->func_id != did->func_id))
			return 0;

		/* if this is a pseudo-multi-function device,
		 * we need explicit matches */
		if (did->match_flags & PCMCIA_DEV_ID_MATCH_DEVICE_NO)
			return 0;
		if (dev->device_no)
			return 0;

		/* also, FUNC_ID matching needs to be activated by userspace
		 * after it has re-checked that there is no possible module
		 * with a prod_id/manf_id/card_id match.
		 */
		if (!dev->allow_func_id_match)
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_FAKE_CIS) {
		if (!dev->socket->fake_cis)
			pcmcia_load_firmware(dev, did->cisfile);

		if (!dev->socket->fake_cis)
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_ANONYMOUS) {
		int i;
		for (i=0; i<4; i++)
			if (dev->prod_id[i])
				return 0;
		if (dev->has_manf_id || dev->has_card_id || dev->has_func_id)
			return 0;
	}

	dev->dev.driver_data = (void *) did;

	return 1;
}


static int pcmcia_bus_match(struct device * dev, struct device_driver * drv) {
	struct pcmcia_device * p_dev = to_pcmcia_dev(dev);
	struct pcmcia_driver * p_drv = to_pcmcia_drv(drv);
	struct pcmcia_device_id *did = p_drv->id_table;

	/* matching by cardmgr */
	if (p_dev->cardmgr == p_drv)
		return 1;

	while (did && did->match_flags) {
		if (pcmcia_devmatch(p_dev, did))
			return 1;
		did++;
	}

	return 0;
}

#ifdef CONFIG_HOTPLUG

static int pcmcia_bus_hotplug(struct device *dev, char **envp, int num_envp,
			      char *buffer, int buffer_size)
{
	struct pcmcia_device *p_dev;
	int i, length = 0;
	u32 hash[4] = { 0, 0, 0, 0};

	if (!dev)
		return -ENODEV;

	p_dev = to_pcmcia_dev(dev);

	/* calculate hashes */
	for (i=0; i<4; i++) {
		if (!p_dev->prod_id[i])
			continue;
		hash[i] = crc32(0, p_dev->prod_id[i], strlen(p_dev->prod_id[i]));
	}

	i = 0;

	if (add_hotplug_env_var(envp, num_envp, &i,
				buffer, buffer_size, &length,
				"SOCKET_NO=%u",
				p_dev->socket->sock))
		return -ENOMEM;

	if (add_hotplug_env_var(envp, num_envp, &i,
				buffer, buffer_size, &length,
				"DEVICE_NO=%02X",
				p_dev->device_no))
		return -ENOMEM;

	if (add_hotplug_env_var(envp, num_envp, &i,
				buffer, buffer_size, &length,
				"MODALIAS=pcmcia:m%04Xc%04Xf%02Xfn%02Xpfn%02X"
				"pa%08Xpb%08Xpc%08Xpd%08X",
				p_dev->has_manf_id ? p_dev->manf_id : 0,
				p_dev->has_card_id ? p_dev->card_id : 0,
				p_dev->has_func_id ? p_dev->func_id : 0,
				p_dev->func,
				p_dev->device_no,
				hash[0],
				hash[1],
				hash[2],
				hash[3]))
		return -ENOMEM;

	envp[i] = NULL;

	return 0;
}

#else

static int pcmcia_bus_hotplug(struct device *dev, char **envp, int num_envp,
			      char *buffer, int buffer_size)
{
	return -ENODEV;
}

#endif

/************************ per-device sysfs output ***************************/

#define pcmcia_device_attr(field, test, format)				\
static ssize_t field##_show (struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);		\
	return p_dev->test ? sprintf (buf, format, p_dev->field) : -ENODEV; \
}

#define pcmcia_device_stringattr(name, field)					\
static ssize_t name##_show (struct device *dev, struct device_attribute *attr, char *buf)		\
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

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	int i;
	u32 hash[4] = { 0, 0, 0, 0};

	/* calculate hashes */
	for (i=0; i<4; i++) {
		if (!p_dev->prod_id[i])
			continue;
		hash[i] = crc32(0,p_dev->prod_id[i],strlen(p_dev->prod_id[i]));
	}
	return sprintf(buf, "pcmcia:m%04Xc%04Xf%02Xfn%02Xpfn%02X"
				"pa%08Xpb%08Xpc%08Xpd%08X\n",
				p_dev->has_manf_id ? p_dev->manf_id : 0,
				p_dev->has_card_id ? p_dev->card_id : 0,
				p_dev->has_func_id ? p_dev->func_id : 0,
				p_dev->func, p_dev->device_no,
				hash[0], hash[1], hash[2], hash[3]);
}

static ssize_t pcmcia_store_allow_func_id_match(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
        if (!count)
                return -EINVAL;

	down(&p_dev->socket->skt_sem);
	p_dev->allow_func_id_match = 1;
	up(&p_dev->socket->skt_sem);

	bus_rescan_devices(&pcmcia_bus_type);

	return count;
}

static struct device_attribute pcmcia_dev_attrs[] = {
	__ATTR(function, 0444, func_show, NULL),
	__ATTR_RO(func_id),
	__ATTR_RO(manf_id),
	__ATTR_RO(card_id),
	__ATTR_RO(prod_id1),
	__ATTR_RO(prod_id2),
	__ATTR_RO(prod_id3),
	__ATTR_RO(prod_id4),
	__ATTR_RO(modalias),
	__ATTR(allow_func_id_match, 0200, NULL, pcmcia_store_allow_func_id_match),
	__ATTR_NULL,
};


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
	struct pcmcia_driver *p_drv;
	struct send_event_data *data = _data;

	/* we get called for all sockets, but may only pass the event
	 * for drivers _on the affected socket_ */
	if (p_dev->socket != data->skt)
		return 0;

	p_drv = to_pcmcia_drv(p_dev->dev.driver);
	if (!p_drv)
		return 0;

	if (p_dev->state & (CLIENT_UNBOUND|CLIENT_STALE))
		return 0;

	if (p_drv->event)
		return p_drv->event(data->event, data->priority,
				    &p_dev->event_callback_args);

	return 0;
}

static int send_event(struct pcmcia_socket *s, event_t event, int priority)
{
	struct send_event_data private;

	private.skt = s;
	private.event = event;
	private.priority = priority;

	return bus_for_each_dev(&pcmcia_bus_type, NULL, &private, send_event_callback);
} /* send_event */


/* Normally, the event is passed to individual drivers after
 * informing userspace. Only for CS_EVENT_CARD_REMOVAL this
 * is inversed to maintain historic compatibility.
 */

static int ds_event(struct pcmcia_socket *skt, event_t event, int priority)
{
	struct pcmcia_socket *s = pcmcia_get_socket(skt);
	int ret = 0;

	ds_dbg(1, "ds_event(0x%06x, %d, 0x%p)\n",
	       event, priority, skt);
    
	switch (event) {

	case CS_EVENT_CARD_REMOVAL:
		s->pcmcia_state.present = 0;
	    	send_event(skt, event, priority);
		unbind_request(skt);
		handle_event(skt, event);
		break;
	
	case CS_EVENT_CARD_INSERTION:
		s->pcmcia_state.present = 1;
		pcmcia_card_add(skt);
		handle_event(skt, event);
		break;

	case CS_EVENT_EJECTION_REQUEST:
		ret = send_event(skt, event, priority);
		break;

	default:
		handle_event(skt, event);
		send_event(skt, event, priority);
		break;
    }

    pcmcia_put_socket(s);

    return 0;
} /* ds_event */



int pcmcia_register_client(struct pcmcia_device **handle, client_reg_t *req)
{
	struct pcmcia_socket *s = NULL;
	struct pcmcia_device *p_dev = NULL;
	struct pcmcia_driver *p_drv = NULL;

	/* Look for unbound client with matching dev_info */
	down_read(&pcmcia_socket_list_rwsem);
	list_for_each_entry(s, &pcmcia_socket_list, socket_list) {
		unsigned long flags;

		if (s->state & SOCKET_CARDBUS)
			continue;

		s = pcmcia_get_socket(s);
		if (!s)
			continue;
		spin_lock_irqsave(&pcmcia_dev_list_lock, flags);
		list_for_each_entry(p_dev, &s->devices_list, socket_device_list) {
			p_dev = pcmcia_get_dev(p_dev);
			if (!p_dev)
				continue;
			if (!(p_dev->state & CLIENT_UNBOUND) ||
			    (!p_dev->dev.driver)) {
				pcmcia_put_dev(p_dev);
				continue;
			}
			p_drv = to_pcmcia_drv(p_dev->dev.driver);
			if (!strncmp(p_drv->drv.name, (char *)req->dev_info, DEV_NAME_LEN)) {
				spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
				goto found;
			}
			pcmcia_put_dev(p_dev);
		}
		spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);
		pcmcia_put_socket(s);
	}
 found:
	up_read(&pcmcia_socket_list_rwsem);
	if (!p_dev)
		return -ENODEV;

	pcmcia_put_socket(s); /* safe, as we already hold a reference from bind_device */

	*handle = p_dev;
	p_dev->state &= ~CLIENT_UNBOUND;
	p_dev->event_callback_args = req->event_callback_args;
	p_dev->event_callback_args.client_handle = p_dev;


	if (!s->functions) {
		cistpl_longlink_mfc_t mfc;
		if (pccard_read_tuple(s, p_dev->func, CISTPL_LONGLINK_MFC, &mfc)
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
	       p_dev, p_dev->dev.bus_id);

	if ((s->state & (SOCKET_PRESENT|SOCKET_CARDBUS)) == SOCKET_PRESENT) {
		if (p_drv->event)
			p_drv->event(CS_EVENT_CARD_INSERTION, CS_EVENT_PRI_LOW,
				     &p_dev->event_callback_args);

	}

	return CS_SUCCESS;

 out_no_resource:
	pcmcia_put_dev(p_dev);
	return CS_OUT_OF_RESOURCE;
} /* register_client */
EXPORT_SYMBOL(pcmcia_register_client);


/* unbind _all_ devices attached to a given pcmcia_bus_socket. The
 * drivers have been called with EVENT_CARD_REMOVAL before.
 */
static int unbind_request(struct pcmcia_socket *s)
{
	struct pcmcia_device	*p_dev;
	unsigned long		flags;

	ds_dbg(2, "unbind_request(%d)\n", s->sock);

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
		p_dev->state |= CLIENT_STALE;
		spin_unlock_irqrestore(&pcmcia_dev_list_lock, flags);

		device_unregister(&p_dev->dev);
	}

	return 0;
} /* unbind_request */

int pcmcia_deregister_client(struct pcmcia_device *p_dev)
{
	struct pcmcia_socket *s;
	int i;

	s = p_dev->socket;
	ds_dbg(1, "deregister_client(%p)\n", p_dev);

	if (p_dev->state & (CLIENT_IRQ_REQ|CLIENT_IO_REQ|CLIENT_CONFIG_LOCKED))
		goto warn_out;
	for (i = 0; i < MAX_WIN; i++)
		if (p_dev->state & CLIENT_WIN_REQ(i))
			goto warn_out;

	if (p_dev->state & CLIENT_STALE) {
		p_dev->state &= ~CLIENT_STALE;
		pcmcia_put_dev(p_dev);
	} else {
		p_dev->state = CLIENT_UNBOUND;
	}

	return CS_SUCCESS;
 warn_out:
	printk(KERN_WARNING "ds: deregister_client was called too early.\n");
	return CS_IN_USE;
} /* deregister_client */
EXPORT_SYMBOL(pcmcia_deregister_client);

static struct pcmcia_callback pcmcia_bus_callback = {
	.owner = THIS_MODULE,
	.event = ds_event,
	.requery = pcmcia_bus_rescan,
};

static int __devinit pcmcia_bus_add_socket(struct class_device *class_dev)
{
	struct pcmcia_socket *socket = class_get_devdata(class_dev);
	int ret;

	socket = pcmcia_get_socket(socket);
	if (!socket) {
		printk(KERN_ERR "PCMCIA obtaining reference to socket %p failed\n", socket);
		return -ENODEV;
	}

	/*
	 * Ugly. But we want to wait for the socket threads to have started up.
	 * We really should let the drivers themselves drive some of this..
	 */
	msleep(250);

#ifdef CONFIG_PCMCIA_IOCTL
	init_waitqueue_head(&socket->queue);
#endif
	INIT_LIST_HEAD(&socket->devices_list);
	INIT_WORK(&socket->device_add, pcmcia_delayed_add_pseudo_device, socket);
	memset(&socket->pcmcia_state, 0, sizeof(u8));
	socket->device_count = 0;

	ret = pccard_register_pcmcia(socket, &pcmcia_bus_callback);
	if (ret) {
		printk(KERN_ERR "PCMCIA registration PCCard core failed for socket %p\n", socket);
		pcmcia_put_socket(socket);
		return (ret);
	}

	return 0;
}

static void pcmcia_bus_remove_socket(struct class_device *class_dev)
{
	struct pcmcia_socket *socket = class_get_devdata(class_dev);

	if (!socket)
		return;

	socket->pcmcia_state.dead = 1;
	pccard_register_pcmcia(socket, NULL);

	pcmcia_put_socket(socket);

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
	.hotplug = pcmcia_bus_hotplug,
	.match = pcmcia_bus_match,
	.dev_attrs = pcmcia_dev_attrs,
};


static int __init init_pcmcia_bus(void)
{
	spin_lock_init(&pcmcia_dev_list_lock);

	bus_register(&pcmcia_bus_type);
	class_interface_register(&pcmcia_bus_interface);

	pcmcia_setup_ioctl();

	return 0;
}
fs_initcall(init_pcmcia_bus); /* one level after subsys_initcall so that 
			       * pcmcia_socket_class is already registered */


static void __exit exit_pcmcia_bus(void)
{
	pcmcia_cleanup_ioctl();

	class_interface_unregister(&pcmcia_bus_interface);

	bus_unregister(&pcmcia_bus_type);
}
module_exit(exit_pcmcia_bus);


MODULE_ALIAS("ds");
