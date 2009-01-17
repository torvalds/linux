/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2007-2008 Ben Backx <ben@bbackx.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>

#include <dmxdev.h>
#include <dvb_demux.h>
#include <dvb_frontend.h>
#include <dvbdev.h>

#include <csr1212.h>
#include <highlevel.h>
#include <hosts.h>
#include <ieee1394_hotplug.h>
#include <nodemgr.h>

#include "avc.h"
#include "cmp.h"
#include "firedtv.h"
#include "firedtv-ci.h"
#include "firedtv-rc.h"

#define MATCH_FLAGS	IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID | \
			IEEE1394_MATCH_SPECIFIER_ID | IEEE1394_MATCH_VERSION
#define DIGITAL_EVERYWHERE_OUI   0x001287

static struct ieee1394_device_id fdtv_id_table[] = {

	{
		/* FloppyDTV S/CI and FloppyDTV S2 */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000024,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	},{
		/* FloppyDTV T/CI */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000025,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	},{
		/* FloppyDTV C/CI */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000026,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	},{
		/* FireDTV S/CI and FloppyDTV S2 */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000034,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	},{
		/* FireDTV T/CI */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000035,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	},{
		/* FireDTV C/CI */
		.match_flags	= MATCH_FLAGS,
		.vendor_id	= DIGITAL_EVERYWHERE_OUI,
		.model_id	= 0x000036,
		.specifier_id	= AVC_UNIT_SPEC_ID_ENTRY,
		.version	= AVC_SW_VERSION_ENTRY,
	}, { }
};

MODULE_DEVICE_TABLE(ieee1394, fdtv_id_table);

/* list of all firedtv devices */
LIST_HEAD(fdtv_list);
DEFINE_SPINLOCK(fdtv_list_lock);

static void fcp_request(struct hpsb_host *host,
			int nodeid,
			int direction,
			int cts,
			u8 *data,
			size_t length)
{
	struct firedtv *fdtv = NULL;
	struct firedtv *fdtv_entry;
	unsigned long flags;

	if (length > 0 && ((data[0] & 0xf0) >> 4) == 0) {

		spin_lock_irqsave(&fdtv_list_lock, flags);
		list_for_each_entry(fdtv_entry,&fdtv_list,list) {
			if (fdtv_entry->ud->ne->host == host &&
			    fdtv_entry->ud->ne->nodeid == nodeid &&
			    (fdtv_entry->subunit == (data[1]&0x7) ||
			     (fdtv_entry->subunit == 0 &&
			      (data[1]&0x7) == 0x7))) {
				fdtv=fdtv_entry;
				break;
			}
		}
		spin_unlock_irqrestore(&fdtv_list_lock, flags);

		if (fdtv)
			avc_recv(fdtv, data, length);
	}
}

const char *fdtv_model_names[] = {
	[FIREDTV_UNKNOWN] = "unknown type",
	[FIREDTV_DVB_S]   = "FireDTV S/CI",
	[FIREDTV_DVB_C]   = "FireDTV C/CI",
	[FIREDTV_DVB_T]   = "FireDTV T/CI",
	[FIREDTV_DVB_S2]  = "FireDTV S2  ",
};

static int fdtv_probe(struct device *dev)
{
	struct unit_directory *ud =
			container_of(dev, struct unit_directory, device);
	struct firedtv *fdtv;
	unsigned long flags;
	int kv_len;
	void *kv_str;
	int i;
	int err = -ENOMEM;

	fdtv = kzalloc(sizeof(*fdtv), GFP_KERNEL);
	if (!fdtv)
		return -ENOMEM;

	dev->driver_data	= fdtv;
	fdtv->ud		= ud;
	fdtv->subunit		= 0;
	fdtv->isochannel	= -1;
	fdtv->tone		= 0xff;
	fdtv->voltage		= 0xff;

	mutex_init(&fdtv->avc_mutex);
	init_waitqueue_head(&fdtv->avc_wait);
	fdtv->avc_reply_received = true;
	mutex_init(&fdtv->demux_mutex);
	INIT_WORK(&fdtv->remote_ctrl_work, avc_remote_ctrl_work);

	/* Reading device model from ROM */
	kv_len = (ud->model_name_kv->value.leaf.len - 2) * sizeof(quadlet_t);
	kv_str = CSR1212_TEXTUAL_DESCRIPTOR_LEAF_DATA(ud->model_name_kv);
	for (i = ARRAY_SIZE(fdtv_model_names); --i;)
		if (strlen(fdtv_model_names[i]) <= kv_len &&
		    strncmp(kv_str, fdtv_model_names[i], kv_len) == 0)
			break;
	fdtv->type = i;

	/*
	 * Work around a bug in udev's path_id script:  Use the fw-host's dev
	 * instead of the unit directory's dev as parent of the input device.
	 */
	err = fdtv_register_rc(fdtv, dev->parent->parent);
	if (err)
		goto fail_free;

	INIT_LIST_HEAD(&fdtv->list);
	spin_lock_irqsave(&fdtv_list_lock, flags);
	list_add_tail(&fdtv->list, &fdtv_list);
	spin_unlock_irqrestore(&fdtv_list_lock, flags);

	err = avc_identify_subunit(fdtv);
	if (err)
		goto fail;

	err = fdtv_dvbdev_init(fdtv, dev);
	if (err)
		goto fail;

	avc_register_remote_control(fdtv);
	return 0;

fail:
	spin_lock_irqsave(&fdtv_list_lock, flags);
	list_del(&fdtv->list);
	spin_unlock_irqrestore(&fdtv_list_lock, flags);
	fdtv_unregister_rc(fdtv);
fail_free:
	kfree(fdtv);
	return err;
}

static int fdtv_remove(struct device *dev)
{
	struct firedtv *fdtv = dev->driver_data;
	unsigned long flags;

	fdtv_ca_release(fdtv);
	dvb_unregister_frontend(&fdtv->fe);
	dvb_net_release(&fdtv->dvbnet);
	fdtv->demux.dmx.close(&fdtv->demux.dmx);
	fdtv->demux.dmx.remove_frontend(&fdtv->demux.dmx,
					   &fdtv->frontend);
	dvb_dmxdev_release(&fdtv->dmxdev);
	dvb_dmx_release(&fdtv->demux);
	dvb_unregister_adapter(&fdtv->adapter);

	spin_lock_irqsave(&fdtv_list_lock, flags);
	list_del(&fdtv->list);
	spin_unlock_irqrestore(&fdtv_list_lock, flags);

	cancel_work_sync(&fdtv->remote_ctrl_work);
	fdtv_unregister_rc(fdtv);

	kfree(fdtv);
	return 0;
}

static int fdtv_update(struct unit_directory *ud)
{
	struct firedtv *fdtv = ud->device.driver_data;

	if (fdtv->isochannel >= 0)
		cmp_establish_pp_connection(fdtv, fdtv->subunit,
					    fdtv->isochannel);
	return 0;
}

static struct hpsb_protocol_driver fdtv_driver = {

	.name		= "firedtv",
	.id_table	= fdtv_id_table,
	.update		= fdtv_update,

	.driver         = {
		//.name and .bus are filled in for us in more recent linux versions
		//.name	= "FireDTV",
		//.bus	= &ieee1394_bus_type,
		.probe  = fdtv_probe,
		.remove = fdtv_remove,
	},
};

static struct hpsb_highlevel fdtv_highlevel = {
	.name		= "firedtv",
	.fcp_request	= fcp_request,
};

static int __init fdtv_init(void)
{
	int ret;

	hpsb_register_highlevel(&fdtv_highlevel);
	ret = hpsb_register_protocol(&fdtv_driver);
	if (ret) {
		printk(KERN_ERR "firedtv: failed to register protocol\n");
		hpsb_unregister_highlevel(&fdtv_highlevel);
	}
	return ret;
}

static void __exit fdtv_exit(void)
{
	hpsb_unregister_protocol(&fdtv_driver);
	hpsb_unregister_highlevel(&fdtv_highlevel);
}

module_init(fdtv_init);
module_exit(fdtv_exit);

MODULE_AUTHOR("Andreas Monitzer <andy@monitzer.com>");
MODULE_AUTHOR("Ben Backx <ben@bbackx.com>");
MODULE_DESCRIPTION("FireDTV DVB Driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("FireDTV DVB");
