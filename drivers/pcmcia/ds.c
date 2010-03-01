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
 * (C) 2003 - 2006	Dominik Brodowski
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/crc32.h>
#include <linux/firmware.h>
#include <linux/kref.h>
#include <linux/dma-mapping.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include <pcmcia/ss.h>

#include "cs_internal.h"

/*====================================================================*/

/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("PCMCIA Driver Services");
MODULE_LICENSE("GPL");


/*====================================================================*/

static void pcmcia_check_driver(struct pcmcia_driver *p_drv)
{
	struct pcmcia_device_id *did = p_drv->id_table;
	unsigned int i;
	u32 hash;

	if (!p_drv->probe || !p_drv->remove)
		printk(KERN_DEBUG "pcmcia: %s lacks a requisite callback "
		       "function\n", p_drv->drv.name);

	while (did && did->match_flags) {
		for (i = 0; i < 4; i++) {
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


/*======================================================================*/


struct pcmcia_dynid {
	struct list_head 		node;
	struct pcmcia_device_id 	id;
};

/**
 * pcmcia_store_new_id - add a new PCMCIA device ID to this driver and re-probe devices
 * @driver: target device driver
 * @buf: buffer for scanning device ID data
 * @count: input size
 *
 * Adds a new dynamic PCMCIA device ID to this driver,
 * and causes the driver to probe for all devices again.
 */
static ssize_t
pcmcia_store_new_id(struct device_driver *driver, const char *buf, size_t count)
{
	struct pcmcia_dynid *dynid;
	struct pcmcia_driver *pdrv = to_pcmcia_drv(driver);
	__u16 match_flags, manf_id, card_id;
	__u8 func_id, function, device_no;
	__u32 prod_id_hash[4] = {0, 0, 0, 0};
	int fields = 0;
	int retval = 0;

	fields = sscanf(buf, "%hx %hx %hx %hhx %hhx %hhx %x %x %x %x",
			&match_flags, &manf_id, &card_id, &func_id, &function, &device_no,
			&prod_id_hash[0], &prod_id_hash[1], &prod_id_hash[2], &prod_id_hash[3]);
	if (fields < 6)
		return -EINVAL;

	dynid = kzalloc(sizeof(struct pcmcia_dynid), GFP_KERNEL);
	if (!dynid)
		return -ENOMEM;

	dynid->id.match_flags = match_flags;
	dynid->id.manf_id = manf_id;
	dynid->id.card_id = card_id;
	dynid->id.func_id = func_id;
	dynid->id.function = function;
	dynid->id.device_no = device_no;
	memcpy(dynid->id.prod_id_hash, prod_id_hash, sizeof(__u32) * 4);

	mutex_lock(&pdrv->dynids.lock);
	list_add_tail(&dynid->node, &pdrv->dynids.list);
	mutex_unlock(&pdrv->dynids.lock);

	if (get_driver(&pdrv->drv)) {
		retval = driver_attach(&pdrv->drv);
		put_driver(&pdrv->drv);
	}

	if (retval)
		return retval;
	return count;
}
static DRIVER_ATTR(new_id, S_IWUSR, NULL, pcmcia_store_new_id);

static void
pcmcia_free_dynids(struct pcmcia_driver *drv)
{
	struct pcmcia_dynid *dynid, *n;

	mutex_lock(&drv->dynids.lock);
	list_for_each_entry_safe(dynid, n, &drv->dynids.list, node) {
		list_del(&dynid->node);
		kfree(dynid);
	}
	mutex_unlock(&drv->dynids.lock);
}

static int
pcmcia_create_newid_file(struct pcmcia_driver *drv)
{
	int error = 0;
	if (drv->probe != NULL)
		error = driver_create_file(&drv->drv, &driver_attr_new_id);
	return error;
}


/**
 * pcmcia_register_driver - register a PCMCIA driver with the bus core
 * @driver: the &driver being registered
 *
 * Registers a PCMCIA driver with the PCMCIA bus core.
 */
int pcmcia_register_driver(struct pcmcia_driver *driver)
{
	int error;

	if (!driver)
		return -EINVAL;

	pcmcia_check_driver(driver);

	/* initialize common fields */
	driver->drv.bus = &pcmcia_bus_type;
	driver->drv.owner = driver->owner;
	mutex_init(&driver->dynids.lock);
	INIT_LIST_HEAD(&driver->dynids.list);

	pr_debug("registering driver %s\n", driver->drv.name);

	error = driver_register(&driver->drv);
	if (error < 0)
		return error;

	error = pcmcia_create_newid_file(driver);
	if (error)
		driver_unregister(&driver->drv);

	return error;
}
EXPORT_SYMBOL(pcmcia_register_driver);

/**
 * pcmcia_unregister_driver - unregister a PCMCIA driver with the bus core
 * @driver: the &driver being unregistered
 */
void pcmcia_unregister_driver(struct pcmcia_driver *driver)
{
	pr_debug("unregistering driver %s\n", driver->drv.name);
	driver_unregister(&driver->drv);
	pcmcia_free_dynids(driver);
}
EXPORT_SYMBOL(pcmcia_unregister_driver);


/* pcmcia_device handling */

struct pcmcia_device *pcmcia_get_dev(struct pcmcia_device *p_dev)
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

static void pcmcia_release_function(struct kref *ref)
{
	struct config_t *c = container_of(ref, struct config_t, ref);
	pr_debug("releasing config_t\n");
	kfree(c);
}

static void pcmcia_release_dev(struct device *dev)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	int i;
	dev_dbg(dev, "releasing device\n");
	pcmcia_put_socket(p_dev->socket);
	for (i = 0; i < 4; i++)
		kfree(p_dev->prod_id[i]);
	kfree(p_dev->devname);
	kref_put(&p_dev->function_config->ref, pcmcia_release_function);
	kfree(p_dev);
}


static int pcmcia_device_probe(struct device *dev)
{
	struct pcmcia_device *p_dev;
	struct pcmcia_driver *p_drv;
	struct pcmcia_socket *s;
	cistpl_config_t cis_config;
	int ret = 0;

	dev = get_device(dev);
	if (!dev)
		return -ENODEV;

	p_dev = to_pcmcia_dev(dev);
	p_drv = to_pcmcia_drv(dev->driver);
	s = p_dev->socket;

	dev_dbg(dev, "trying to bind to %s\n", p_drv->drv.name);

	if ((!p_drv->probe) || (!p_dev->function_config) ||
	    (!try_module_get(p_drv->owner))) {
		ret = -EINVAL;
		goto put_dev;
	}

	/* set up some more device information */
	ret = pccard_read_tuple(p_dev->socket, p_dev->func, CISTPL_CONFIG,
				&cis_config);
	if (!ret) {
		p_dev->conf.ConfigBase = cis_config.base;
		p_dev->conf.Present = cis_config.rmask[0];
	} else {
		dev_printk(KERN_INFO, dev,
			   "pcmcia: could not parse base and rmask0 of CIS\n");
		p_dev->conf.ConfigBase = 0;
		p_dev->conf.Present = 0;
	}

	ret = p_drv->probe(p_dev);
	if (ret) {
		dev_dbg(dev, "binding to %s failed with %d\n",
			   p_drv->drv.name, ret);
		goto put_module;
	}

	mutex_lock(&s->ops_mutex);
	if ((s->pcmcia_state.has_pfc) &&
	    (p_dev->socket->device_count == 1) && (p_dev->device_no == 0))
		pcmcia_parse_uevents(s, PCMCIA_UEVENT_REQUERY);
	mutex_unlock(&s->ops_mutex);

put_module:
	if (ret)
		module_put(p_drv->owner);
put_dev:
	if (ret)
		put_device(dev);
	return ret;
}


/*
 * Removes a PCMCIA card from the device tree and socket list.
 */
static void pcmcia_card_remove(struct pcmcia_socket *s, struct pcmcia_device *leftover)
{
	struct pcmcia_device	*p_dev;
	struct pcmcia_device	*tmp;

	dev_dbg(leftover ? &leftover->dev : &s->dev,
		   "pcmcia_card_remove(%d) %s\n", s->sock,
		   leftover ? leftover->devname : "");

	mutex_lock(&s->ops_mutex);
	if (!leftover)
		s->device_count = 0;
	else
		s->device_count = 1;
	mutex_unlock(&s->ops_mutex);

	/* unregister all pcmcia_devices registered with this socket, except leftover */
	list_for_each_entry_safe(p_dev, tmp, &s->devices_list, socket_device_list) {
		if (p_dev == leftover)
			continue;

		mutex_lock(&s->ops_mutex);
		list_del(&p_dev->socket_device_list);
		p_dev->_removed = 1;
		mutex_unlock(&s->ops_mutex);

		dev_dbg(&p_dev->dev, "unregistering device\n");
		device_unregister(&p_dev->dev);
	}

	return;
}

static int pcmcia_device_remove(struct device *dev)
{
	struct pcmcia_device *p_dev;
	struct pcmcia_driver *p_drv;
	int i;

	p_dev = to_pcmcia_dev(dev);
	p_drv = to_pcmcia_drv(dev->driver);

	dev_dbg(dev, "removing device\n");

	/* If we're removing the primary module driving a
	 * pseudo multi-function card, we need to unbind
	 * all devices
	 */
	if ((p_dev->socket->pcmcia_state.has_pfc) &&
	    (p_dev->socket->device_count > 0) &&
	    (p_dev->device_no == 0))
		pcmcia_card_remove(p_dev->socket, p_dev);

	/* detach the "instance" */
	if (!p_drv)
		return 0;

	if (p_drv->remove)
		p_drv->remove(p_dev);

	p_dev->dev_node = NULL;

	/* check for proper unloading */
	if (p_dev->_irq || p_dev->_io || p_dev->_locked)
		dev_printk(KERN_INFO, dev,
			"pcmcia: driver %s did not release config properly\n",
			p_drv->drv.name);

	for (i = 0; i < MAX_WIN; i++)
		if (p_dev->_win & CLIENT_WIN_REQ(i))
			dev_printk(KERN_INFO, dev,
			  "pcmcia: driver %s did not release window properly\n",
			   p_drv->drv.name);

	/* references from pcmcia_probe_device */
	pcmcia_put_dev(p_dev);
	module_put(p_drv->owner);

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

	if (!pccard_read_tuple(p_dev->socket, BIND_FN_ALL,
			       CISTPL_MANFID, &manf_id)) {
		mutex_lock(&p_dev->socket->ops_mutex);
		p_dev->manf_id = manf_id.manf;
		p_dev->card_id = manf_id.card;
		p_dev->has_manf_id = 1;
		p_dev->has_card_id = 1;
		mutex_unlock(&p_dev->socket->ops_mutex);
	}

	if (!pccard_read_tuple(p_dev->socket, p_dev->func,
			       CISTPL_FUNCID, &func_id)) {
		mutex_lock(&p_dev->socket->ops_mutex);
		p_dev->func_id = func_id.func;
		p_dev->has_func_id = 1;
		mutex_unlock(&p_dev->socket->ops_mutex);
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
			dev_dbg(&p_dev->dev,
				   "mem device geometry probably means "
				   "FUNCID_MEMORY\n");
			mutex_lock(&p_dev->socket->ops_mutex);
			p_dev->func_id = CISTPL_FUNCID_MEMORY;
			p_dev->has_func_id = 1;
			mutex_unlock(&p_dev->socket->ops_mutex);
		}
		kfree(devgeo);
	}

	if (!pccard_read_tuple(p_dev->socket, BIND_FN_ALL, CISTPL_VERS_1,
			       vers1)) {
		mutex_lock(&p_dev->socket->ops_mutex);
		for (i = 0; i < min_t(unsigned int, 4, vers1->ns); i++) {
			char *tmp;
			unsigned int length;
			char *new;

			tmp = vers1->str + vers1->ofs[i];

			length = strlen(tmp) + 1;
			if ((length < 2) || (length > 255))
				continue;

			new = kmalloc(sizeof(char) * length, GFP_KERNEL);
			if (!new)
				continue;

			new = strncpy(new, tmp, length);

			tmp = p_dev->prod_id[i];
			p_dev->prod_id[i] = new;
			kfree(tmp);
		}
		mutex_unlock(&p_dev->socket->ops_mutex);
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
static DEFINE_MUTEX(device_add_lock);

struct pcmcia_device *pcmcia_device_add(struct pcmcia_socket *s, unsigned int function)
{
	struct pcmcia_device *p_dev, *tmp_dev;
	int i;

	s = pcmcia_get_socket(s);
	if (!s)
		return NULL;

	mutex_lock(&device_add_lock);

	pr_debug("adding device to %d, function %d\n", s->sock, function);

	p_dev = kzalloc(sizeof(struct pcmcia_device), GFP_KERNEL);
	if (!p_dev)
		goto err_put;

	mutex_lock(&s->ops_mutex);
	p_dev->device_no = (s->device_count++);
	mutex_unlock(&s->ops_mutex);

	/* max of 2 devices per card */
	if (p_dev->device_no >= 2)
		goto err_free;

	p_dev->socket = s;
	p_dev->func   = function;

	p_dev->dev.bus = &pcmcia_bus_type;
	p_dev->dev.parent = s->dev.parent;
	p_dev->dev.release = pcmcia_release_dev;
	/* by default don't allow DMA */
	p_dev->dma_mask = DMA_MASK_NONE;
	p_dev->dev.dma_mask = &p_dev->dma_mask;
	dev_set_name(&p_dev->dev, "%d.%d", p_dev->socket->sock, p_dev->device_no);
	if (!dev_name(&p_dev->dev))
		goto err_free;
	p_dev->devname = kasprintf(GFP_KERNEL, "pcmcia%s", dev_name(&p_dev->dev));
	if (!p_dev->devname)
		goto err_free;
	dev_dbg(&p_dev->dev, "devname is %s\n", p_dev->devname);

	mutex_lock(&s->ops_mutex);

	/*
	 * p_dev->function_config must be the same for all card functions.
	 * Note that this is serialized by the device_add_lock, so that
	 * only one such struct will be created.
	 */
	list_for_each_entry(tmp_dev, &s->devices_list, socket_device_list)
		if (p_dev->func == tmp_dev->func) {
			p_dev->function_config = tmp_dev->function_config;
			p_dev->io = tmp_dev->io;
			p_dev->irq = tmp_dev->irq;
			kref_get(&p_dev->function_config->ref);
		}

	/* Add to the list in pcmcia_bus_socket */
	list_add(&p_dev->socket_device_list, &s->devices_list);

	mutex_unlock(&s->ops_mutex);

	if (!p_dev->function_config) {
		dev_dbg(&p_dev->dev, "creating config_t\n");
		p_dev->function_config = kzalloc(sizeof(struct config_t),
						 GFP_KERNEL);
		if (!p_dev->function_config)
			goto err_unreg;
		kref_init(&p_dev->function_config->ref);
	}

	dev_printk(KERN_NOTICE, &p_dev->dev,
		   "pcmcia: registering new device %s\n",
		   p_dev->devname);

	pcmcia_device_query(p_dev);

	if (device_register(&p_dev->dev))
		goto err_unreg;

	mutex_unlock(&device_add_lock);

	return p_dev;

 err_unreg:
	mutex_lock(&s->ops_mutex);
	list_del(&p_dev->socket_device_list);
	mutex_unlock(&s->ops_mutex);

 err_free:
	mutex_lock(&s->ops_mutex);
	s->device_count--;
	mutex_unlock(&s->ops_mutex);

	for (i = 0; i < 4; i++)
		kfree(p_dev->prod_id[i]);
	kfree(p_dev->devname);
	kfree(p_dev);
 err_put:
	mutex_unlock(&device_add_lock);
	pcmcia_put_socket(s);

	return NULL;
}


static int pcmcia_card_add(struct pcmcia_socket *s)
{
	cistpl_longlink_mfc_t mfc;
	unsigned int no_funcs, i, no_chains;
	int ret = -EAGAIN;

	mutex_lock(&s->ops_mutex);
	if (!(s->resource_setup_done)) {
		dev_dbg(&s->dev,
			   "no resources available, delaying card_add\n");
		mutex_unlock(&s->ops_mutex);
		return -EAGAIN; /* try again, but later... */
	}

	if (pcmcia_validate_mem(s)) {
		dev_dbg(&s->dev, "validating mem resources failed, "
		       "delaying card_add\n");
		mutex_unlock(&s->ops_mutex);
		return -EAGAIN; /* try again, but later... */
	}
	mutex_unlock(&s->ops_mutex);

	ret = pccard_validate_cis(s, &no_chains);
	if (ret || !no_chains) {
		dev_dbg(&s->dev, "invalid CIS or invalid resources\n");
		return -ENODEV;
	}

	if (!pccard_read_tuple(s, BIND_FN_ALL, CISTPL_LONGLINK_MFC, &mfc))
		no_funcs = mfc.nfn;
	else
		no_funcs = 1;
	s->functions = no_funcs;

	for (i = 0; i < no_funcs; i++)
		pcmcia_device_add(s, i);

	return ret;
}


static int pcmcia_requery_callback(struct device *dev, void * _data)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	if (!p_dev->dev.driver) {
		dev_dbg(dev, "update device information\n");
		pcmcia_device_query(p_dev);
	}

	return 0;
}


static void pcmcia_requery(struct pcmcia_socket *s)
{
	int present, has_pfc;

	mutex_lock(&s->ops_mutex);
	present = s->pcmcia_state.present;
	mutex_unlock(&s->ops_mutex);

	if (!present)
		return;

	if (s->functions == 0) {
		pcmcia_card_add(s);
		return;
	}

	/* some device information might have changed because of a CIS
	 * update or because we can finally read it correctly... so
	 * determine it again, overwriting old values if necessary. */
	bus_for_each_dev(&pcmcia_bus_type, NULL, NULL, pcmcia_requery_callback);

	/* if the CIS changed, we need to check whether the number of
	 * functions changed. */
	if (s->fake_cis) {
		int old_funcs, new_funcs;
		cistpl_longlink_mfc_t mfc;

		/* does this cis override add or remove functions? */
		old_funcs = s->functions;

		if (!pccard_read_tuple(s, BIND_FN_ALL, CISTPL_LONGLINK_MFC,
					&mfc))
			new_funcs = mfc.nfn;
		else
			new_funcs = 1;
		if (old_funcs > new_funcs) {
			pcmcia_card_remove(s, NULL);
			pcmcia_card_add(s);
		} else if (new_funcs > old_funcs) {
			s->functions = new_funcs;
			pcmcia_device_add(s, 1);
		}
	}

	/* If the PCMCIA device consists of two pseudo devices,
	 * call pcmcia_device_add() -- which will fail if both
	 * devices are already registered. */
	mutex_lock(&s->ops_mutex);
	has_pfc = s->pcmcia_state.has_pfc;
	mutex_unlock(&s->ops_mutex);
	if (has_pfc)
		pcmcia_device_add(s, 0);

	/* we re-scan all devices, not just the ones connected to this
	 * socket. This does not matter, though. */
	if (bus_rescan_devices(&pcmcia_bus_type))
		dev_warn(&s->dev, "rescanning the bus failed\n");
}


#ifdef CONFIG_PCMCIA_LOAD_CIS

/**
 * pcmcia_load_firmware - load CIS from userspace if device-provided is broken
 * @dev: the pcmcia device which needs a CIS override
 * @filename: requested filename in /lib/firmware/
 *
 * This uses the in-kernel firmware loading mechanism to use a "fake CIS" if
 * the one provided by the card is broken. The firmware files reside in
 * /lib/firmware/ in userspace.
 */
static int pcmcia_load_firmware(struct pcmcia_device *dev, char * filename)
{
	struct pcmcia_socket *s = dev->socket;
	const struct firmware *fw;
	int ret = -ENOMEM;

	if (!filename)
		return -EINVAL;

	dev_dbg(&dev->dev, "trying to load CIS file %s\n", filename);

	if (request_firmware(&fw, filename, &dev->dev) == 0) {
		if (fw->size >= CISTPL_MAX_CIS_SIZE) {
			ret = -EINVAL;
			dev_printk(KERN_ERR, &dev->dev,
				   "pcmcia: CIS override is too big\n");
			goto release;
		}

		if (!pcmcia_replace_cis(s, fw->data, fw->size))
			ret = 0;
		else {
			dev_printk(KERN_ERR, &dev->dev,
				   "pcmcia: CIS override failed\n");
			goto release;
		}


		/* update information */
		pcmcia_device_query(dev);

		/* requery (as number of functions might have changed) */
		pcmcia_parse_uevents(s, PCMCIA_UEVENT_REQUERY);
	}
 release:
	release_firmware(fw);

	return ret;
}

#else /* !CONFIG_PCMCIA_LOAD_CIS */

static inline int pcmcia_load_firmware(struct pcmcia_device *dev, char * filename)
{
	return -ENODEV;
}

#endif


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
		if (dev->device_no != did->device_no)
			return 0;
		mutex_lock(&dev->socket->ops_mutex);
		dev->socket->pcmcia_state.has_pfc = 1;
		mutex_unlock(&dev->socket->ops_mutex);
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_FUNC_ID) {
		int ret;

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
		mutex_lock(&dev->socket->ops_mutex);
		ret = dev->allow_func_id_match;
		mutex_unlock(&dev->socket->ops_mutex);

		if (!ret) {
			dev_dbg(&dev->dev,
				"skipping FUNC_ID match until userspace ACK\n");
			return 0;
		}
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_FAKE_CIS) {
		dev_dbg(&dev->dev, "device needs a fake CIS\n");
		if (!dev->socket->fake_cis)
			pcmcia_load_firmware(dev, did->cisfile);

		if (!dev->socket->fake_cis)
			return 0;
	}

	if (did->match_flags & PCMCIA_DEV_ID_MATCH_ANONYMOUS) {
		int i;
		for (i = 0; i < 4; i++)
			if (dev->prod_id[i])
				return 0;
		if (dev->has_manf_id || dev->has_card_id || dev->has_func_id)
			return 0;
	}

	return 1;
}


static int pcmcia_bus_match(struct device *dev, struct device_driver *drv)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	struct pcmcia_driver *p_drv = to_pcmcia_drv(drv);
	struct pcmcia_device_id *did = p_drv->id_table;
	struct pcmcia_dynid *dynid;

	/* match dynamic devices first */
	mutex_lock(&p_drv->dynids.lock);
	list_for_each_entry(dynid, &p_drv->dynids.list, node) {
		dev_dbg(dev, "trying to match to %s\n", drv->name);
		if (pcmcia_devmatch(p_dev, &dynid->id)) {
			dev_dbg(dev, "matched to %s\n", drv->name);
			mutex_unlock(&p_drv->dynids.lock);
			return 1;
		}
	}
	mutex_unlock(&p_drv->dynids.lock);

#ifdef CONFIG_PCMCIA_IOCTL
	/* matching by cardmgr */
	if (p_dev->cardmgr == p_drv) {
		dev_dbg(dev, "cardmgr matched to %s\n", drv->name);
		return 1;
	}
#endif

	while (did && did->match_flags) {
		dev_dbg(dev, "trying to match to %s\n", drv->name);
		if (pcmcia_devmatch(p_dev, did)) {
			dev_dbg(dev, "matched to %s\n", drv->name);
			return 1;
		}
		did++;
	}

	return 0;
}

#ifdef CONFIG_HOTPLUG

static int pcmcia_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct pcmcia_device *p_dev;
	int i;
	u32 hash[4] = { 0, 0, 0, 0};

	if (!dev)
		return -ENODEV;

	p_dev = to_pcmcia_dev(dev);

	/* calculate hashes */
	for (i = 0; i < 4; i++) {
		if (!p_dev->prod_id[i])
			continue;
		hash[i] = crc32(0, p_dev->prod_id[i], strlen(p_dev->prod_id[i]));
	}

	if (add_uevent_var(env, "SOCKET_NO=%u", p_dev->socket->sock))
		return -ENOMEM;

	if (add_uevent_var(env, "DEVICE_NO=%02X", p_dev->device_no))
		return -ENOMEM;

	if (add_uevent_var(env, "MODALIAS=pcmcia:m%04Xc%04Xf%02Xfn%02Xpfn%02X"
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

	return 0;
}

#else

static int pcmcia_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	return -ENODEV;
}

#endif

/************************ runtime PM support ***************************/

static int pcmcia_dev_suspend(struct device *dev, pm_message_t state);
static int pcmcia_dev_resume(struct device *dev);

static int runtime_suspend(struct device *dev)
{
	int rc;

	down(&dev->sem);
	rc = pcmcia_dev_suspend(dev, PMSG_SUSPEND);
	up(&dev->sem);
	return rc;
}

static int runtime_resume(struct device *dev)
{
	int rc;

	down(&dev->sem);
	rc = pcmcia_dev_resume(dev);
	up(&dev->sem);
	return rc;
}

/************************ per-device sysfs output ***************************/

#define pcmcia_device_attr(field, test, format)				\
static ssize_t field##_show (struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);		\
	return p_dev->test ? sprintf(buf, format, p_dev->field) : -ENODEV; \
}

#define pcmcia_device_stringattr(name, field)					\
static ssize_t name##_show (struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);		\
	return p_dev->field ? sprintf(buf, "%s\n", p_dev->field) : -ENODEV; \
}

pcmcia_device_attr(func, socket, "0x%02x\n");
pcmcia_device_attr(func_id, has_func_id, "0x%02x\n");
pcmcia_device_attr(manf_id, has_manf_id, "0x%04x\n");
pcmcia_device_attr(card_id, has_card_id, "0x%04x\n");
pcmcia_device_stringattr(prod_id1, prod_id[0]);
pcmcia_device_stringattr(prod_id2, prod_id[1]);
pcmcia_device_stringattr(prod_id3, prod_id[2]);
pcmcia_device_stringattr(prod_id4, prod_id[3]);


static ssize_t pcmcia_show_pm_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);

	if (p_dev->suspended)
		return sprintf(buf, "off\n");
	else
		return sprintf(buf, "on\n");
}

static ssize_t pcmcia_store_pm_state(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	int ret = 0;

	if (!count)
		return -EINVAL;

	if ((!p_dev->suspended) && !strncmp(buf, "off", 3))
		ret = runtime_suspend(dev);
	else if (p_dev->suspended && !strncmp(buf, "on", 2))
		ret = runtime_resume(dev);

	return ret ? ret : count;
}


static ssize_t modalias_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	int i;
	u32 hash[4] = { 0, 0, 0, 0};

	/* calculate hashes */
	for (i = 0; i < 4; i++) {
		if (!p_dev->prod_id[i])
			continue;
		hash[i] = crc32(0, p_dev->prod_id[i],
				strlen(p_dev->prod_id[i]));
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

	mutex_lock(&p_dev->socket->ops_mutex);
	p_dev->allow_func_id_match = 1;
	mutex_unlock(&p_dev->socket->ops_mutex);
	pcmcia_parse_uevents(p_dev->socket, PCMCIA_UEVENT_REQUERY);

	return count;
}

static struct device_attribute pcmcia_dev_attrs[] = {
	__ATTR(function, 0444, func_show, NULL),
	__ATTR(pm_state, 0644, pcmcia_show_pm_state, pcmcia_store_pm_state),
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

/* PM support, also needed for reset */

static int pcmcia_dev_suspend(struct device *dev, pm_message_t state)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	struct pcmcia_driver *p_drv = NULL;
	int ret = 0;

	mutex_lock(&p_dev->socket->ops_mutex);
	if (p_dev->suspended) {
		mutex_unlock(&p_dev->socket->ops_mutex);
		return 0;
	}
	p_dev->suspended = 1;
	mutex_unlock(&p_dev->socket->ops_mutex);

	dev_dbg(dev, "suspending\n");

	if (dev->driver)
		p_drv = to_pcmcia_drv(dev->driver);

	if (!p_drv)
		goto out;

	if (p_drv->suspend) {
		ret = p_drv->suspend(p_dev);
		if (ret) {
			dev_printk(KERN_ERR, dev,
				   "pcmcia: device %s (driver %s) did "
				   "not want to go to sleep (%d)\n",
				   p_dev->devname, p_drv->drv.name, ret);
			mutex_lock(&p_dev->socket->ops_mutex);
			p_dev->suspended = 0;
			mutex_unlock(&p_dev->socket->ops_mutex);
			goto out;
		}
	}

	if (p_dev->device_no == p_dev->func) {
		dev_dbg(dev, "releasing configuration\n");
		pcmcia_release_configuration(p_dev);
	}

 out:
	return ret;
}


static int pcmcia_dev_resume(struct device *dev)
{
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);
	struct pcmcia_driver *p_drv = NULL;
	int ret = 0;

	mutex_lock(&p_dev->socket->ops_mutex);
	if (!p_dev->suspended) {
		mutex_unlock(&p_dev->socket->ops_mutex);
		return 0;
	}
	p_dev->suspended = 0;
	mutex_unlock(&p_dev->socket->ops_mutex);

	dev_dbg(dev, "resuming\n");

	if (dev->driver)
		p_drv = to_pcmcia_drv(dev->driver);

	if (!p_drv)
		goto out;

	if (p_dev->device_no == p_dev->func) {
		dev_dbg(dev, "requesting configuration\n");
		ret = pcmcia_request_configuration(p_dev, &p_dev->conf);
		if (ret)
			goto out;
	}

	if (p_drv->resume)
		ret = p_drv->resume(p_dev);

 out:
	return ret;
}


static int pcmcia_bus_suspend_callback(struct device *dev, void * _data)
{
	struct pcmcia_socket *skt = _data;
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);

	if (p_dev->socket != skt || p_dev->suspended)
		return 0;

	return runtime_suspend(dev);
}

static int pcmcia_bus_resume_callback(struct device *dev, void * _data)
{
	struct pcmcia_socket *skt = _data;
	struct pcmcia_device *p_dev = to_pcmcia_dev(dev);

	if (p_dev->socket != skt || !p_dev->suspended)
		return 0;

	runtime_resume(dev);

	return 0;
}

static int pcmcia_bus_resume(struct pcmcia_socket *skt)
{
	dev_dbg(&skt->dev, "resuming socket %d\n", skt->sock);
	bus_for_each_dev(&pcmcia_bus_type, NULL, skt, pcmcia_bus_resume_callback);
	return 0;
}

static int pcmcia_bus_suspend(struct pcmcia_socket *skt)
{
	dev_dbg(&skt->dev, "suspending socket %d\n", skt->sock);
	if (bus_for_each_dev(&pcmcia_bus_type, NULL, skt,
			     pcmcia_bus_suspend_callback)) {
		pcmcia_bus_resume(skt);
		return -EIO;
	}
	return 0;
}


/*======================================================================

    The card status event handler.

======================================================================*/

/* Normally, the event is passed to individual drivers after
 * informing userspace. Only for CS_EVENT_CARD_REMOVAL this
 * is inversed to maintain historic compatibility.
 */

static int ds_event(struct pcmcia_socket *skt, event_t event, int priority)
{
	struct pcmcia_socket *s = pcmcia_get_socket(skt);

	if (!s) {
		dev_printk(KERN_ERR, &skt->dev,
			   "PCMCIA obtaining reference to socket "	\
			   "failed, event 0x%x lost!\n", event);
		return -ENODEV;
	}

	dev_dbg(&skt->dev, "ds_event(0x%06x, %d, 0x%p)\n",
		   event, priority, skt);

	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		mutex_lock(&s->ops_mutex);
		s->pcmcia_state.present = 0;
		mutex_unlock(&s->ops_mutex);
		pcmcia_card_remove(skt, NULL);
		handle_event(skt, event);
		mutex_lock(&s->ops_mutex);
		destroy_cis_cache(s);
		mutex_unlock(&s->ops_mutex);
		break;

	case CS_EVENT_CARD_INSERTION:
		mutex_lock(&s->ops_mutex);
		s->pcmcia_state.has_pfc = 0;
		s->pcmcia_state.present = 1;
		destroy_cis_cache(s); /* to be on the safe side... */
		mutex_unlock(&s->ops_mutex);
		pcmcia_card_add(skt);
		handle_event(skt, event);
		break;

	case CS_EVENT_EJECTION_REQUEST:
		break;

	case CS_EVENT_PM_RESUME:
		if (verify_cis_cache(skt) != 0) {
			dev_dbg(&skt->dev, "cis mismatch - different card\n");
			/* first, remove the card */
			ds_event(skt, CS_EVENT_CARD_REMOVAL, CS_EVENT_PRI_HIGH);
			mutex_lock(&s->ops_mutex);
			destroy_cis_cache(skt);
			kfree(skt->fake_cis);
			skt->fake_cis = NULL;
			mutex_unlock(&s->ops_mutex);
			/* now, add the new card */
			ds_event(skt, CS_EVENT_CARD_INSERTION,
				 CS_EVENT_PRI_LOW);
		}
		handle_event(skt, event);
		break;

	case CS_EVENT_PM_SUSPEND:
	case CS_EVENT_RESET_PHYSICAL:
	case CS_EVENT_CARD_RESET:
	default:
		handle_event(skt, event);
		break;
    }

    pcmcia_put_socket(s);

    return 0;
} /* ds_event */


struct pcmcia_device *pcmcia_dev_present(struct pcmcia_device *_p_dev)
{
	struct pcmcia_device *p_dev;
	struct pcmcia_device *ret = NULL;

	p_dev = pcmcia_get_dev(_p_dev);
	if (!p_dev)
		return NULL;

	mutex_lock(&p_dev->socket->ops_mutex);
	if (!p_dev->socket->pcmcia_state.present)
		goto out;

	if (p_dev->socket->pcmcia_state.dead)
		goto out;

	if (p_dev->_removed)
		goto out;

	if (p_dev->suspended)
		goto out;

	ret = p_dev;
 out:
	mutex_unlock(&p_dev->socket->ops_mutex);
	pcmcia_put_dev(p_dev);
	return ret;
}
EXPORT_SYMBOL(pcmcia_dev_present);


static struct pcmcia_callback pcmcia_bus_callback = {
	.owner = THIS_MODULE,
	.event = ds_event,
	.requery = pcmcia_requery,
	.validate = pccard_validate_cis,
	.suspend = pcmcia_bus_suspend,
	.resume = pcmcia_bus_resume,
};

static int __devinit pcmcia_bus_add_socket(struct device *dev,
					   struct class_interface *class_intf)
{
	struct pcmcia_socket *socket = dev_get_drvdata(dev);
	int ret;

	socket = pcmcia_get_socket(socket);
	if (!socket) {
		dev_printk(KERN_ERR, dev,
			   "PCMCIA obtaining reference to socket failed\n");
		return -ENODEV;
	}

	ret = sysfs_create_bin_file(&dev->kobj, &pccard_cis_attr);
	if (ret) {
		dev_printk(KERN_ERR, dev, "PCMCIA registration failed\n");
		pcmcia_put_socket(socket);
		return ret;
	}

#ifdef CONFIG_PCMCIA_IOCTL
	init_waitqueue_head(&socket->queue);
#endif
	INIT_LIST_HEAD(&socket->devices_list);
	memset(&socket->pcmcia_state, 0, sizeof(u8));
	socket->device_count = 0;

	ret = pccard_register_pcmcia(socket, &pcmcia_bus_callback);
	if (ret) {
		dev_printk(KERN_ERR, dev, "PCMCIA registration failed\n");
		pcmcia_put_socket(socket);
		return ret;
	}

	return 0;
}

static void pcmcia_bus_remove_socket(struct device *dev,
				     struct class_interface *class_intf)
{
	struct pcmcia_socket *socket = dev_get_drvdata(dev);

	if (!socket)
		return;

	mutex_lock(&socket->ops_mutex);
	socket->pcmcia_state.dead = 1;
	mutex_unlock(&socket->ops_mutex);

	pccard_register_pcmcia(socket, NULL);

	/* unregister any unbound devices */
	mutex_lock(&socket->skt_mutex);
	pcmcia_card_remove(socket, NULL);
	release_cis_mem(socket);
	mutex_unlock(&socket->skt_mutex);

	sysfs_remove_bin_file(&dev->kobj, &pccard_cis_attr);

	pcmcia_put_socket(socket);

	return;
}


/* the pcmcia_bus_interface is used to handle pcmcia socket devices */
static struct class_interface pcmcia_bus_interface __refdata = {
	.class = &pcmcia_socket_class,
	.add_dev = &pcmcia_bus_add_socket,
	.remove_dev = &pcmcia_bus_remove_socket,
};


struct bus_type pcmcia_bus_type = {
	.name = "pcmcia",
	.uevent = pcmcia_bus_uevent,
	.match = pcmcia_bus_match,
	.dev_attrs = pcmcia_dev_attrs,
	.probe = pcmcia_device_probe,
	.remove = pcmcia_device_remove,
	.suspend = pcmcia_dev_suspend,
	.resume = pcmcia_dev_resume,
};


static int __init init_pcmcia_bus(void)
{
	int ret;

	ret = bus_register(&pcmcia_bus_type);
	if (ret < 0) {
		printk(KERN_WARNING "pcmcia: bus_register error: %d\n", ret);
		return ret;
	}
	ret = class_interface_register(&pcmcia_bus_interface);
	if (ret < 0) {
		printk(KERN_WARNING
			"pcmcia: class_interface_register error: %d\n", ret);
		bus_unregister(&pcmcia_bus_type);
		return ret;
	}

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
