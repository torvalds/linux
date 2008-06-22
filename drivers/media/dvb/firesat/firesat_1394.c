/*
 * FireSAT DVB driver
 *
 * Copyright (c) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (c) 2007-2008 Ben Backx <ben@bbackx.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <ieee1394_hotplug.h>
#include <nodemgr.h>
#include <highlevel.h>
#include <ohci1394.h>
#include <hosts.h>
#include <dvbdev.h>

#include "firesat.h"
#include "avc_api.h"
#include "cmp.h"
#include "firesat-rc.h"
#include "firesat-ci.h"

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

static void firesat_add_host(struct hpsb_host *host);
static void firesat_remove_host(struct hpsb_host *host);
static void firesat_host_reset(struct hpsb_host *host);

/*
static void iso_receive(struct hpsb_host *host, int channel, quadlet_t *data,
			size_t length);
*/

static void fcp_request(struct hpsb_host *host,
			int nodeid,
			int direction,
			int cts,
			u8 *data,
			size_t length);

static struct hpsb_highlevel firesat_highlevel = {
	.name		= "FireSAT",
	.add_host	= firesat_add_host,
	.remove_host	= firesat_remove_host,
	.host_reset	= firesat_host_reset,
// FIXME	.iso_receive =	iso_receive,
	.fcp_request	= fcp_request,
};

static void firesat_add_host (struct hpsb_host *host)
{
	struct ti_ohci *ohci = (struct ti_ohci *)host->hostdata;

	/* We only work with the OHCI-1394 driver */
	if (strcmp(host->driver->name, OHCI1394_DRIVER_NAME))
		return;

	if (!hpsb_create_hostinfo(&firesat_highlevel, host, 0)) {
		printk(KERN_ERR "Cannot allocate hostinfo\n");
		return;
	}

	hpsb_set_hostinfo(&firesat_highlevel, host, ohci);
	hpsb_set_hostinfo_key(&firesat_highlevel, host, ohci->host->id);
}

static void firesat_remove_host (struct hpsb_host *host)
{

}

static void firesat_host_reset(struct hpsb_host *host)
{
    printk(KERN_INFO "FireSAT host_reset (nodeid = 0x%x, hosts active = %d)\n",host->node_id,host->nodes_active);
}

struct firewireheader {
    union {
	struct {
	    unsigned char tcode:4;
	    unsigned char sy:4;
	    unsigned char tag:2;
	    unsigned char channel:6;

	    unsigned char length_l;
	    unsigned char length_h;
	} hdr;
	unsigned long val;
    };
};

struct CIPHeader {
    union {
	struct {
	    unsigned char syncbits:2;
	    unsigned char sid:6;
	    unsigned char dbs;
	    unsigned char fn:2;
	    unsigned char qpc:3;
	    unsigned char sph:1;
	    unsigned char rsv:2;
	    unsigned char dbc;
	    unsigned char syncbits2:2;
	    unsigned char fmt:6;
	    unsigned long fdf:24;
	} cip;
	unsigned long long val;
    };
};

struct MPEG2Header {
    union {
	struct {
	    unsigned char sync; // must be 0x47
	    unsigned char transport_error_indicator:1;
	    unsigned char payload_unit_start_indicator:1;
	    unsigned char transport_priority:1;
	    unsigned short pid:13;
	    unsigned char transport_scrambling_control:2;
	    unsigned char adaption_field_control:2;
	    unsigned char continuity_counter:4;
	} hdr;
	unsigned long val;
    };
};

#if 0
static void iso_receive(struct hpsb_host *host,
			int channel,
			quadlet_t *data,
			size_t length)
{
	struct firesat *firesat = NULL;
	struct firesat *firesat_entry;
	unsigned long flags;

//    printk(KERN_INFO "FireSAT iso_receive: channel %d, length = %d\n", channel, length);

	if (length <= 12)
		return; // ignore empty packets
	else {

		spin_lock_irqsave(&firesat_list_lock, flags);
		list_for_each_entry(firesat_entry,&firesat_list,list) {
			if(firesat_entry->host == host && firesat_entry->isochannel == channel) {
				firesat=firesat_entry;
				break;
			}
		}
		spin_unlock_irqrestore(&firesat_list_lock, flags);

		if (firesat) {
			char *buf= ((char*)data) + sizeof(struct firewireheader)+sizeof(struct CIPHeader);
			int count = (length-sizeof(struct CIPHeader)) / 192;

//			printk(KERN_INFO "%s: length = %u\n data[0] = %08x\n data[1] = %08x\n data[2] = %08x\n data[3] = %08x\n data[4] = %08x\n",__func__, length, data[0],data[1],data[2],data[3],data[4]);

			while (count--) {

				if (buf[sizeof(quadlet_t) /*timestamp*/] == 0x47)
					dvb_dmx_swfilter_packets(&firesat->demux, &buf[sizeof(quadlet_t)], 1);
				else
					printk("%s: invalid packet, skipping\n", __func__);
				buf += 188 + sizeof (quadlet_t) /* timestamp */;
			}
		}
	}
}
#endif

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
	} // else ignore
}

static int firesat_probe(struct device *dev)
{
	struct unit_directory *ud = container_of(dev, struct unit_directory, device);
	struct firesat *firesat;
	struct dvb_frontend *fe;
	unsigned long flags;
	int result;
	unsigned char subunitcount = 0xff, subunit;
	struct firesat **firesats = kmalloc(sizeof (void*) * 2,GFP_KERNEL);
	int kv_len;
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

		if (!(firesat->respfrm = kmalloc(sizeof (AVCRspFrm), GFP_KERNEL))) {
			printk("%s: couldn't allocate memory.\n", __func__);
			kfree(firesat);
			return -ENOMEM;
		}

		sema_init(&firesat->avc_sem, 1);
		atomic_set(&firesat->avc_reply_received, 1);
		sema_init(&firesat->demux_sem, 1);
		atomic_set(&firesat->reschedule_remotecontrol, 0);

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

		/* Determining the device model */
		if (strcmp(kv_buf, "FireDTV S/CI") == 0) {
			printk(KERN_INFO "%s: found DVB/S\n", __func__);
			firesat->type = 1;
		} else if (strcmp(kv_buf, "FireDTV C/CI") == 0) {
			printk(KERN_INFO "%s: found DVB/C\n", __func__);
			firesat->type = 2;
		} else if (strcmp(kv_buf, "FireDTV T/CI") == 0) {
			printk(KERN_INFO "%s: found DVB/T\n", __func__);
			firesat->type = 3;
		} else if (strcmp(kv_buf, "FireDTV S2  ") == 0) {
			printk(KERN_INFO "%s: found DVB/S2\n", __func__);
			firesat->type = 4;
		}
		kfree(kv_buf);

		if (AVCIdentifySubunit(firesat, NULL, (int*)&firesat->type, &firesat->has_ci)) {
			printk("%s: cannot identify subunit %d\n", __func__, subunit);
			spin_lock_irqsave(&firesat_list_lock, flags);
			list_del(&firesat->list);
			spin_unlock_irqrestore(&firesat_list_lock, flags);
			kfree(firesat);
			continue;
		}

// ----
		firesat_dvbdev_init(firesat, dev, fe);
// ----
		firesats[subunit] = firesat;
	} // loop for all tuners

	//beta ;-) Disable remote control stuff to avoid crashing
	//if(firesats[0])
	//	AVCRegisterRemoteControl(firesats[0]);

    return 0;
}

static int firesat_remove(struct device *dev)
{
	struct unit_directory *ud = container_of(dev, struct unit_directory, device);
	struct dvb_frontend* fe;
	struct firesat **firesats = ud->device.driver_data;
	int k;
	unsigned long flags;

	if (firesats) {
		for (k = 0; k < 2; k++)
			if (firesats[k]) {
				if (firesats[k]->has_ci)
					firesat_ca_release(firesats[k]);

#if 0
				if (!(fe = kmalloc(sizeof (struct dvb_frontend), GFP_KERNEL))) {
					fe->ops = firesat_ops;
					fe->dvb = firesats[k]->adapter;

					dvb_unregister_frontend(fe);
					kfree(fe);
				}
#endif
				dvb_net_release(&firesats[k]->dvbnet);
				firesats[k]->demux.dmx.close(&firesats[k]->demux.dmx);
				firesats[k]->demux.dmx.remove_frontend(&firesats[k]->demux.dmx, &firesats[k]->frontend);
				dvb_dmxdev_release(&firesats[k]->dmxdev);
				dvb_dmx_release(&firesats[k]->demux);
				dvb_unregister_adapter(firesats[k]->adapter);

				spin_lock_irqsave(&firesat_list_lock, flags);
				list_del(&firesats[k]->list);
				spin_unlock_irqrestore(&firesat_list_lock, flags);

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

	.name		= "FireSAT",
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

static int __init firesat_init(void)
{
	int ret;

	printk(KERN_INFO "FireSAT loaded\n");
	hpsb_register_highlevel(&firesat_highlevel);
	ret = hpsb_register_protocol(&firesat_driver);
	if (ret) {
		printk(KERN_ERR "FireSAT: failed to register protocol\n");
		hpsb_unregister_highlevel(&firesat_highlevel);
		return ret;
	}

	//Crash in this function, just disable RC for the time being...
	//Don't forget to uncomment in firesat_exit and firesat_probe when you enable this.
	/*if((ret=firesat_register_rc()))
		printk("%s: firesat_register_rc return error code %d (ignored)\n", __func__, ret);*/

	return 0;
}

static void __exit firesat_exit(void)
{
	hpsb_unregister_protocol(&firesat_driver);
	hpsb_unregister_highlevel(&firesat_highlevel);
	printk(KERN_INFO "FireSAT quit\n");
}

module_init(firesat_init);
module_exit(firesat_exit);

MODULE_AUTHOR("Andreas Monitzer <andy@monitzer.com>");
MODULE_AUTHOR("Ben Backx <ben@bbackx.com>");
MODULE_DESCRIPTION("FireSAT DVB Driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("FireSAT DVB");
