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
#include <asm/atomic.h>

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

#define FIRESAT_Vendor_ID   0x001287

static struct ieee1394_device_id firesat_id_table[] = {

	{
		/* FloppyDTV S/CI and FloppyDTV S2 */
		.match_flags = IEEE1394_MATCH_MODEL_ID | IEEE1394_MATCH_SPECIFIER_ID,
		.model_id = 0x000024,
		.specifier_id = AVC_UNIT_SPEC_ID_ENTRY & 0xffffff,
	},{
		/* FloppyDTV T/CI */
		.match_flags = IEEE1394_MATCH_MODEL_ID | IEEE1394_MATCH_SPECIFIER_ID,
		.model_id = 0x000025,
		.specifier_id = AVC_UNIT_SPEC_ID_ENTRY & 0xffffff,
	},{
		/* FloppyDTV C/CI */
		.match_flags = IEEE1394_MATCH_MODEL_ID | IEEE1394_MATCH_SPECIFIER_ID,
		.model_id = 0x000026,
		.specifier_id = AVC_UNIT_SPEC_ID_ENTRY & 0xffffff,
	},{
		/* FireDTV S/CI and FloppyDTV S2 */
		.match_flags = IEEE1394_MATCH_MODEL_ID | IEEE1394_MATCH_SPECIFIER_ID,
		.model_id = 0x000034,
		.specifier_id = AVC_UNIT_SPEC_ID_ENTRY & 0xffffff,
	},{
		/* FireDTV T/CI */
		.match_flags = IEEE1394_MATCH_MODEL_ID | IEEE1394_MATCH_SPECIFIER_ID,
		.model_id = 0x000035,
		.specifier_id = AVC_UNIT_SPEC_ID_ENTRY & 0xffffff,
	},{
		/* FireDTV C/CI */
		.match_flags = IEEE1394_MATCH_MODEL_ID | IEEE1394_MATCH_SPECIFIER_ID,
		.model_id = 0x000036,
		.specifier_id = AVC_UNIT_SPEC_ID_ENTRY & 0xffffff,
	}, { }
};

MODULE_DEVICE_TABLE(ieee1394, firesat_id_table);

/* list of all firesat devices */
LIST_HEAD(firesat_list);
spinlock_t firesat_list_lock = SPIN_LOCK_UNLOCKED;

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
			if (firesat_entry->host == host &&
			    firesat_entry->nodeentry->nodeid == nodeid &&
			    (firesat_entry->subunit == (data[1]&0x7) ||
			     (firesat_entry->subunit == 0 &&
			      (data[1]&0x7) == 0x7))) {
				firesat=firesat_entry;
				break;
			}
		}
		spin_unlock_irqrestore(&firesat_list_lock, flags);

		if (firesat)
			AVCRecv(firesat,data,length);
		else
			printk("%s: received fcp request from unknown source, ignored\n", __func__);
	}
	else
	  printk("%s: received invalid fcp request, ignored\n", __func__);
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
	struct unit_directory *ud = container_of(dev, struct unit_directory, device);
	struct firesat *firesat;
	struct dvb_frontend *fe;
	unsigned long flags;
	unsigned char subunitcount = 0xff, subunit;
	struct firesat **firesats = kmalloc(sizeof (void*) * 2,GFP_KERNEL);
	int kv_len;
	int i;
	char *kv_buf;

	if (!firesats) {
		printk("%s: couldn't allocate memory.\n", __func__);
		return -ENOMEM;
	}

//    printk(KERN_INFO "FireSAT: Detected device with GUID %08lx%04lx%04lx\n",(unsigned long)((ud->ne->guid)>>32),(unsigned long)(ud->ne->guid & 0xFFFF),(unsigned long)ud->ne->guid_vendor_id);
	printk(KERN_INFO "%s: loading device\n", __func__);

	firesats[0] = NULL;
	firesats[1] = NULL;

	ud->device.driver_data = firesats;

	for (subunit = 0; subunit < subunitcount; subunit++) {

		if (!(firesat = kmalloc(sizeof (struct firesat), GFP_KERNEL)) ||
		    !(fe = kmalloc(sizeof (struct dvb_frontend), GFP_KERNEL))) {

			printk("%s: couldn't allocate memory.\n", __func__);
			kfree(firesats);
			return -ENOMEM;
		}

		memset(firesat, 0, sizeof (struct firesat));

		firesat->host		= ud->ne->host;
		firesat->guid		= ud->ne->guid;
		firesat->guid_vendor_id = ud->ne->guid_vendor_id;
		firesat->nodeentry	= ud->ne;
		firesat->isochannel	= -1;
		firesat->tone		= 0xff;
		firesat->voltage	= 0xff;
		firesat->fe             = fe;

		if (!(firesat->respfrm = kmalloc(sizeof (AVCRspFrm), GFP_KERNEL))) {
			printk("%s: couldn't allocate memory.\n", __func__);
			kfree(firesat);
			return -ENOMEM;
		}

		mutex_init(&firesat->avc_mutex);
		init_waitqueue_head(&firesat->avc_wait);
		atomic_set(&firesat->avc_reply_received, 1);
		mutex_init(&firesat->demux_mutex);
		INIT_WORK(&firesat->remote_ctrl_work, avc_remote_ctrl_work);

		spin_lock_irqsave(&firesat_list_lock, flags);
		INIT_LIST_HEAD(&firesat->list);
		list_add_tail(&firesat->list, &firesat_list);
		spin_unlock_irqrestore(&firesat_list_lock, flags);

		if (subunit == 0) {
			firesat->subunit = 0x7; // 0x7 = don't care
			if (AVCSubUnitInfo(firesat, &subunitcount)) {
				printk("%s: AVC subunit info command failed.\n",__func__);
				spin_lock_irqsave(&firesat_list_lock, flags);
				list_del(&firesat->list);
				spin_unlock_irqrestore(&firesat_list_lock, flags);
				kfree(firesat);
				return -EIO;
			}
		}

		printk(KERN_INFO "%s: subunit count = %d\n", __func__, subunitcount);

		firesat->subunit = subunit;

		/* Reading device model from ROM */
		kv_len = (ud->model_name_kv->value.leaf.len - 2) *
			sizeof(quadlet_t);
		kv_buf = kmalloc((sizeof(quadlet_t) * kv_len), GFP_KERNEL);
		memcpy(kv_buf,
			CSR1212_TEXTUAL_DESCRIPTOR_LEAF_DATA(ud->model_name_kv),
			kv_len);
		while ((kv_buf + kv_len - 1) == '\0') kv_len--;
		kv_buf[kv_len++] = '\0';

		for (i = ARRAY_SIZE(firedtv_model_names); --i;)
			if (strcmp(kv_buf, firedtv_model_names[i]) == 0)
				break;
		firesat->type = i;
		kfree(kv_buf);

		if (AVCIdentifySubunit(firesat)) {
			printk("%s: cannot identify subunit %d\n", __func__, subunit);
			spin_lock_irqsave(&firesat_list_lock, flags);
			list_del(&firesat->list);
			spin_unlock_irqrestore(&firesat_list_lock, flags);
			kfree(firesat);
			continue;
		}

// ----
		/* FIXME: check for error return */
		firesat_dvbdev_init(firesat, dev, fe);
// ----
		firesats[subunit] = firesat;
	} // loop for all tuners

	if (firesats[0])
		AVCRegisterRemoteControl(firesats[0]);

    return 0;
}

static int firesat_remove(struct device *dev)
{
	struct unit_directory *ud = container_of(dev, struct unit_directory, device);
	struct firesat **firesats = ud->device.driver_data;
	int k;
	unsigned long flags;

	if (firesats) {
		for (k = 0; k < 2; k++)
			if (firesats[k]) {
					firesat_ca_release(firesats[k]);

				dvb_unregister_frontend(firesats[k]->fe);
				dvb_net_release(&firesats[k]->dvbnet);
				firesats[k]->demux.dmx.close(&firesats[k]->demux.dmx);
				firesats[k]->demux.dmx.remove_frontend(&firesats[k]->demux.dmx, &firesats[k]->frontend);
				dvb_dmxdev_release(&firesats[k]->dmxdev);
				dvb_dmx_release(&firesats[k]->demux);
				dvb_unregister_adapter(firesats[k]->adapter);

				spin_lock_irqsave(&firesat_list_lock, flags);
				list_del(&firesats[k]->list);
				spin_unlock_irqrestore(&firesat_list_lock, flags);

				cancel_work_sync(&firesats[k]->remote_ctrl_work);

				kfree(firesats[k]->fe);
				kfree(firesats[k]->adapter);
				kfree(firesats[k]->respfrm);
				kfree(firesats[k]);
			}
		kfree(firesats);
	} else
		printk("%s: can't get firesat handle\n", __func__);

	printk(KERN_INFO "FireSAT: Removing device with vendor id 0x%x, model id 0x%x.\n",ud->vendor_id,ud->model_id);

	return 0;
}

static int firesat_update(struct unit_directory *ud)
{
	struct firesat **firesats = ud->device.driver_data;
	int k;
	// loop over subunits

	for (k = 0; k < 2; k++)
		if (firesats[k]) {
			firesats[k]->nodeentry = ud->ne;

			if (firesats[k]->isochannel >= 0)
				try_CMPEstablishPPconnection(firesats[k], firesats[k]->subunit, firesats[k]->isochannel);
		}

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
		goto fail;
	}

	ret = firesat_register_rc();
	if (ret) {
		printk(KERN_ERR "firedtv: failed to register input device\n");
		goto fail_rc;
	}

	return 0;
fail_rc:
	hpsb_unregister_protocol(&firesat_driver);
fail:
	hpsb_unregister_highlevel(&firesat_highlevel);
	return ret;
}

static void __exit firesat_exit(void)
{
	firesat_unregister_rc();
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
