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

#include "avc_api.h"
#include "cmp.h"
#include "firesat.h"
#include "firesat-ci.h"
#include "firesat-rc.h"

#define MATCH_FLAGS	IEEE1394_MATCH_VENDOR_ID | IEEE1394_MATCH_MODEL_ID | \
			IEEE1394_MATCH_SPECIFIER_ID | IEEE1394_MATCH_VERSION
#define DIGITAL_EVERYWHERE_OUI   0x001287

static struct ieee1394_device_id firesat_id_table[] = {

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

MODULE_DEVICE_TABLE(ieee1394, firesat_id_table);

/* list of all firesat devices */
LIST_HEAD(firesat_list);
DEFINE_SPINLOCK(firesat_list_lock);

static void fcp_request(struct hpsb_host *host,
			int nodeid,
			int direction,
			int cts,
			u8 *data,
			size_t length)
{
	struct firesat *firesat = NULL;
	struct firesat *firesat_entry;
	unsigned long flags;

	if (length > 0 && ((data[0] & 0xf0) >> 4) == 0) {

		spin_lock_irqsave(&firesat_list_lock, flags);
		list_for_each_entry(firesat_entry,&firesat_list,list) {
			if (firesat_entry->ud->ne->host == host &&
			    firesat_entry->ud->ne->nodeid == nodeid &&
			    (firesat_entry->subunit == (data[1]&0x7) ||
			     (firesat_entry->subunit == 0 &&
			      (data[1]&0x7) == 0x7))) {
				firesat=firesat_entry;
				break;
			}
		}
		spin_unlock_irqrestore(&firesat_list_lock, flags);

		if (firesat)
			avc_recv(firesat, data, length);
	}
}

const char *firedtv_model_names[] = {
	[FireSAT_UNKNOWN] = "unknown type",
	[FireSAT_DVB_S]   = "FireDTV S/CI",
	[FireSAT_DVB_C]   = "FireDTV C/CI",
	[FireSAT_DVB_T]   = "FireDTV T/CI",
	[FireSAT_DVB_S2]  = "FireDTV S2  ",
};

static int firesat_probe(struct device *dev)
{
	struct unit_directory *ud =
			container_of(dev, struct unit_directory, device);
	struct firesat *firesat;
	unsigned long flags;
	int kv_len;
	void *kv_str;
	int i;
	int err = -ENOMEM;

	firesat = kzalloc(sizeof(*firesat), GFP_KERNEL);
	if (!firesat)
		return -ENOMEM;

	dev->driver_data = firesat;
	firesat->ud		= ud;
	firesat->subunit	= 0;
	firesat->isochannel	= -1;
	firesat->tone		= 0xff;
	firesat->voltage	= 0xff;

	mutex_init(&firesat->avc_mutex);
	init_waitqueue_head(&firesat->avc_wait);
	firesat->avc_reply_received = true;
	mutex_init(&firesat->demux_mutex);
	INIT_WORK(&firesat->remote_ctrl_work, avc_remote_ctrl_work);

	/* Reading device model from ROM */
	kv_len = (ud->model_name_kv->value.leaf.len - 2) * sizeof(quadlet_t);
	kv_str = CSR1212_TEXTUAL_DESCRIPTOR_LEAF_DATA(ud->model_name_kv);
	for (i = ARRAY_SIZE(firedtv_model_names); --i;)
		if (strlen(firedtv_model_names[i]) <= kv_len &&
		    strncmp(kv_str, firedtv_model_names[i], kv_len) == 0)
			break;
	firesat->type = i;

	/*
	 * Work around a bug in udev's path_id script:  Use the fw-host's dev
	 * instead of the unit directory's dev as parent of the input device.
	 */
	err = firesat_register_rc(firesat, dev->parent->parent);
	if (err)
		goto fail_free;

	INIT_LIST_HEAD(&firesat->list);
	spin_lock_irqsave(&firesat_list_lock, flags);
	list_add_tail(&firesat->list, &firesat_list);
	spin_unlock_irqrestore(&firesat_list_lock, flags);

	err = avc_identify_subunit(firesat);
	if (err)
		goto fail;

	err = firesat_dvbdev_init(firesat, dev);
	if (err)
		goto fail;

	avc_register_remote_control(firesat);
	return 0;

fail:
	spin_lock_irqsave(&firesat_list_lock, flags);
	list_del(&firesat->list);
	spin_unlock_irqrestore(&firesat_list_lock, flags);
	firesat_unregister_rc(firesat);
fail_free:
	kfree(firesat);
	return err;
}

static int firesat_remove(struct device *dev)
{
	struct firesat *firesat = dev->driver_data;
	unsigned long flags;

	firesat_ca_release(firesat);
	dvb_unregister_frontend(&firesat->fe);
	dvb_net_release(&firesat->dvbnet);
	firesat->demux.dmx.close(&firesat->demux.dmx);
	firesat->demux.dmx.remove_frontend(&firesat->demux.dmx,
					   &firesat->frontend);
	dvb_dmxdev_release(&firesat->dmxdev);
	dvb_dmx_release(&firesat->demux);
	dvb_unregister_adapter(&firesat->adapter);

	spin_lock_irqsave(&firesat_list_lock, flags);
	list_del(&firesat->list);
	spin_unlock_irqrestore(&firesat_list_lock, flags);

	cancel_work_sync(&firesat->remote_ctrl_work);
	firesat_unregister_rc(firesat);

	kfree(firesat);
	return 0;
}

static int firesat_update(struct unit_directory *ud)
{
	struct firesat *firesat = ud->device.driver_data;

	if (firesat->isochannel >= 0)
		cmp_establish_pp_connection(firesat, firesat->subunit,
					    firesat->isochannel);
	return 0;
}

static struct hpsb_protocol_driver firesat_driver = {

	.name		= "firedtv",
	.id_table	= firesat_id_table,
	.update		= firesat_update,

	.driver         = {
		//.name and .bus are filled in for us in more recent linux versions
		//.name	= "FireSAT",
		//.bus	= &ieee1394_bus_type,
		.probe  = firesat_probe,
		.remove = firesat_remove,
	},
};

static struct hpsb_highlevel firesat_highlevel = {
	.name		= "firedtv",
	.fcp_request	= fcp_request,
};

static int __init firesat_init(void)
{
	int ret;

	hpsb_register_highlevel(&firesat_highlevel);
	ret = hpsb_register_protocol(&firesat_driver);
	if (ret) {
		printk(KERN_ERR "firedtv: failed to register protocol\n");
		hpsb_unregister_highlevel(&firesat_highlevel);
	}
	return ret;
}

static void __exit firesat_exit(void)
{
	hpsb_unregister_protocol(&firesat_driver);
	hpsb_unregister_highlevel(&firesat_highlevel);
}

module_init(firesat_init);
module_exit(firesat_exit);

MODULE_AUTHOR("Andreas Monitzer <andy@monitzer.com>");
MODULE_AUTHOR("Ben Backx <ben@bbackx.com>");
MODULE_DESCRIPTION("FireDTV DVB Driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("FireDTV DVB");
