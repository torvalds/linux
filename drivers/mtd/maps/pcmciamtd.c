/*
 * $Id: pcmciamtd.c,v 1.51 2004/07/12 22:38:29 dwmw2 Exp $
 *
 * pcmciamtd.c - MTD driver for PCMCIA flash memory cards
 *
 * Author: Simon Evans <spse@secret.org.uk>
 *
 * Copyright (C) 2002 Simon Evans
 *
 * Licence: GPL
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/system.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>

#ifdef CONFIG_MTD_DEBUG
static int debug = CONFIG_MTD_DEBUG_VERBOSE;
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Set Debug Level 0=quiet, 5=noisy");
#undef DEBUG
#define DEBUG(n, format, arg...) \
	if (n <= debug) {	 \
		printk(KERN_DEBUG __FILE__ ":%s(): " format "\n", __FUNCTION__ , ## arg); \
	}

#else
#undef DEBUG
#define DEBUG(n, arg...)
static const int debug = 0;
#endif

#define err(format, arg...) printk(KERN_ERR "pcmciamtd: " format "\n" , ## arg)
#define info(format, arg...) printk(KERN_INFO "pcmciamtd: " format "\n" , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "pcmciamtd: " format "\n" , ## arg)


#define DRIVER_DESC	"PCMCIA Flash memory card driver"
#define DRIVER_VERSION	"$Revision: 1.51 $"

/* Size of the PCMCIA address space: 26 bits = 64 MB */
#define MAX_PCMCIA_ADDR	0x4000000

struct pcmciamtd_dev {
	dev_link_t	link;		/* PCMCIA link */
	dev_node_t	node;		/* device node */
	caddr_t		win_base;	/* ioremapped address of PCMCIA window */
	unsigned int	win_size;	/* size of window */
	unsigned int	offset;		/* offset into card the window currently points at */
	struct map_info	pcmcia_map;
	struct mtd_info	*mtd_info;
	int		vpp;
	char		mtd_name[sizeof(struct cistpl_vers_1_t)];
};


static dev_info_t dev_info = "pcmciamtd";
static dev_link_t *dev_list;

/* Module parameters */

/* 2 = do 16-bit transfers, 1 = do 8-bit transfers */
static int bankwidth = 2;

/* Speed of memory accesses, in ns */
static int mem_speed;

/* Force the size of an SRAM card */
static int force_size;

/* Force Vpp */
static int vpp;

/* Set Vpp */
static int setvpp;

/* Force card to be treated as FLASH, ROM or RAM */
static int mem_type;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Evans <spse@secret.org.uk>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_PARM(bankwidth, "i");
MODULE_PARM_DESC(bankwidth, "Set bankwidth (1=8 bit, 2=16 bit, default=2)");
MODULE_PARM(mem_speed, "i");
MODULE_PARM_DESC(mem_speed, "Set memory access speed in ns");
MODULE_PARM(force_size, "i");
MODULE_PARM_DESC(force_size, "Force size of card in MiB (1-64)");
MODULE_PARM(setvpp, "i");
MODULE_PARM_DESC(setvpp, "Set Vpp (0=Never, 1=On writes, 2=Always on, default=0)");
MODULE_PARM(vpp, "i");
MODULE_PARM_DESC(vpp, "Vpp value in 1/10ths eg 33=3.3V 120=12V (Dangerous)");
MODULE_PARM(mem_type, "i");
MODULE_PARM_DESC(mem_type, "Set Memory type (0=Flash, 1=RAM, 2=ROM, default=0)");


/* read/write{8,16} copy_{from,to} routines with window remapping to access whole card */
static caddr_t remap_window(struct map_info *map, unsigned long to)
{
	struct pcmciamtd_dev *dev = (struct pcmciamtd_dev *)map->map_priv_1;
	window_handle_t win = (window_handle_t)map->map_priv_2;
	memreq_t mrq;
	int ret;

	if(!(dev->link.state & DEV_PRESENT)) {
		DEBUG(1, "device removed state = 0x%4.4X", dev->link.state);
		return 0;
	}

	mrq.CardOffset = to & ~(dev->win_size-1);
	if(mrq.CardOffset != dev->offset) {
		DEBUG(2, "Remapping window from 0x%8.8x to 0x%8.8x",
		      dev->offset, mrq.CardOffset);
		mrq.Page = 0;
		if( (ret = pcmcia_map_mem_page(win, &mrq)) != CS_SUCCESS) {
			cs_error(dev->link.handle, MapMemPage, ret);
			return NULL;
		}
		dev->offset = mrq.CardOffset;
	}
	return dev->win_base + (to & (dev->win_size-1));
}


static map_word pcmcia_read8_remap(struct map_info *map, unsigned long ofs)
{
	caddr_t addr;
	map_word d = {{0}};

	addr = remap_window(map, ofs);
	if(!addr)
		return d;

	d.x[0] = readb(addr);
	DEBUG(3, "ofs = 0x%08lx (%p) data = 0x%02x", ofs, addr, d.x[0]);
	return d;
}


static map_word pcmcia_read16_remap(struct map_info *map, unsigned long ofs)
{
	caddr_t addr;
	map_word d = {{0}};

	addr = remap_window(map, ofs);
	if(!addr)
		return d;

	d.x[0] = readw(addr);
	DEBUG(3, "ofs = 0x%08lx (%p) data = 0x%04x", ofs, addr, d.x[0]);
	return d;
}


static void pcmcia_copy_from_remap(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	struct pcmciamtd_dev *dev = (struct pcmciamtd_dev *)map->map_priv_1;
	unsigned long win_size = dev->win_size;

	DEBUG(3, "to = %p from = %lu len = %u", to, from, len);
	while(len) {
		int toread = win_size - (from & (win_size-1));
		caddr_t addr;

		if(toread > len)
			toread = len;
		
		addr = remap_window(map, from);
		if(!addr)
			return;

		DEBUG(4, "memcpy from %p to %p len = %d", addr, to, toread);
		memcpy_fromio(to, addr, toread);
		len -= toread;
		to += toread;
		from += toread;
	}
}


static void pcmcia_write8_remap(struct map_info *map, map_word d, unsigned long adr)
{
	caddr_t addr = remap_window(map, adr);

	if(!addr)
		return;

	DEBUG(3, "adr = 0x%08lx (%p)  data = 0x%02x", adr, addr, d.x[0]);
	writeb(d.x[0], addr);
}


static void pcmcia_write16_remap(struct map_info *map, map_word d, unsigned long adr)
{
	caddr_t addr = remap_window(map, adr);
	if(!addr)
		return;

	DEBUG(3, "adr = 0x%08lx (%p)  data = 0x%04x", adr, addr, d.x[0]);
	writew(d.x[0], addr);
}


static void pcmcia_copy_to_remap(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	struct pcmciamtd_dev *dev = (struct pcmciamtd_dev *)map->map_priv_1;
	unsigned long win_size = dev->win_size;

	DEBUG(3, "to = %lu from = %p len = %u", to, from, len);
	while(len) {
		int towrite = win_size - (to & (win_size-1));
		caddr_t addr;

		if(towrite > len)
			towrite = len;

		addr = remap_window(map, to);
		if(!addr)
			return;

		DEBUG(4, "memcpy from %p to %p len = %d", from, addr, towrite);
		memcpy_toio(addr, from, towrite);
		len -= towrite;
		to += towrite;
		from += towrite;
	}
}


/* read/write{8,16} copy_{from,to} routines with direct access */

#define DEV_REMOVED(x)  (!(*(u_int *)x->map_priv_1 & DEV_PRESENT))

static map_word pcmcia_read8(struct map_info *map, unsigned long ofs)
{
	caddr_t win_base = (caddr_t)map->map_priv_2;
	map_word d = {{0}};

	if(DEV_REMOVED(map))
		return d;

	d.x[0] = readb(win_base + ofs);
	DEBUG(3, "ofs = 0x%08lx (%p) data = 0x%02x", ofs, win_base + ofs, d.x[0]);
	return d;
}


static map_word pcmcia_read16(struct map_info *map, unsigned long ofs)
{
	caddr_t win_base = (caddr_t)map->map_priv_2;
	map_word d = {{0}};

	if(DEV_REMOVED(map))
		return d;

	d.x[0] = readw(win_base + ofs);
	DEBUG(3, "ofs = 0x%08lx (%p) data = 0x%04x", ofs, win_base + ofs, d.x[0]);
	return d;
}


static void pcmcia_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	caddr_t win_base = (caddr_t)map->map_priv_2;

	if(DEV_REMOVED(map))
		return;

	DEBUG(3, "to = %p from = %lu len = %u", to, from, len);
	memcpy_fromio(to, win_base + from, len);
}


static void pcmcia_write8(struct map_info *map, u8 d, unsigned long adr)
{
	caddr_t win_base = (caddr_t)map->map_priv_2;

	if(DEV_REMOVED(map))
		return;

	DEBUG(3, "adr = 0x%08lx (%p)  data = 0x%02x", adr, win_base + adr, d);
	writeb(d, win_base + adr);
}


static void pcmcia_write16(struct map_info *map, u16 d, unsigned long adr)
{
	caddr_t win_base = (caddr_t)map->map_priv_2;

	if(DEV_REMOVED(map))
		return;

	DEBUG(3, "adr = 0x%08lx (%p)  data = 0x%04x", adr, win_base + adr, d);
	writew(d, win_base + adr);
}


static void pcmcia_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	caddr_t win_base = (caddr_t)map->map_priv_2;

	if(DEV_REMOVED(map))
		return;

	DEBUG(3, "to = %lu from = %p len = %u", to, from, len);
	memcpy_toio(win_base + to, from, len);
}


static void pcmciamtd_set_vpp(struct map_info *map, int on)
{
	struct pcmciamtd_dev *dev = (struct pcmciamtd_dev *)map->map_priv_1;
	dev_link_t *link = &dev->link;
	modconf_t mod;
	int ret;

	mod.Attributes = CONF_VPP1_CHANGE_VALID | CONF_VPP2_CHANGE_VALID;
	mod.Vcc = 0;
	mod.Vpp1 = mod.Vpp2 = on ? dev->vpp : 0;

	DEBUG(2, "dev = %p on = %d vpp = %d\n", dev, on, dev->vpp);
	ret = pcmcia_modify_configuration(link->handle, &mod);
	if(ret != CS_SUCCESS) {
		cs_error(link->handle, ModifyConfiguration, ret);
	}
}


/* After a card is removed, pcmciamtd_release() will unregister the
 * device, and release the PCMCIA configuration.  If the device is
 * still open, this will be postponed until it is closed.
 */

static void pcmciamtd_release(dev_link_t *link)
{
	struct pcmciamtd_dev *dev = link->priv;

	DEBUG(3, "link = 0x%p", link);

	if (link->win) {
		if(dev->win_base) {
			iounmap(dev->win_base);
			dev->win_base = NULL;
		}
		pcmcia_release_window(link->win);
	}
	pcmcia_release_configuration(link->handle);
	link->state &= ~DEV_CONFIG;
}


static void card_settings(struct pcmciamtd_dev *dev, dev_link_t *link, int *new_name)
{
	int rc;
	tuple_t tuple;
	cisparse_t parse;
	u_char buf[64];

	tuple.Attributes = 0;
	tuple.TupleData = (cisdata_t *)buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	tuple.DesiredTuple = RETURN_FIRST_TUPLE;

	rc = pcmcia_get_first_tuple(link->handle, &tuple);
	while(rc == CS_SUCCESS) {
		rc = pcmcia_get_tuple_data(link->handle, &tuple);
		if(rc != CS_SUCCESS) {
			cs_error(link->handle, GetTupleData, rc);
			break;
		}
		rc = pcmcia_parse_tuple(link->handle, &tuple, &parse);
		if(rc != CS_SUCCESS) {
			cs_error(link->handle, ParseTuple, rc);
			break;
		}
		
		switch(tuple.TupleCode) {
		case  CISTPL_FORMAT: {
			cistpl_format_t *t = &parse.format;
			(void)t; /* Shut up, gcc */
			DEBUG(2, "Format type: %u, Error Detection: %u, offset = %u, length =%u",
			      t->type, t->edc, t->offset, t->length);
			break;
			
		}
			
		case CISTPL_DEVICE: {
			cistpl_device_t *t = &parse.device;
			int i;
			DEBUG(2, "Common memory:");
			dev->pcmcia_map.size = t->dev[0].size;
			for(i = 0; i < t->ndev; i++) {
				DEBUG(2, "Region %d, type = %u", i, t->dev[i].type);
				DEBUG(2, "Region %d, wp = %u", i, t->dev[i].wp);
				DEBUG(2, "Region %d, speed = %u ns", i, t->dev[i].speed);
				DEBUG(2, "Region %d, size = %u bytes", i, t->dev[i].size);
			}
			break;
		}
			
		case CISTPL_VERS_1: {
			cistpl_vers_1_t *t = &parse.version_1;
			int i;
			if(t->ns) {
				dev->mtd_name[0] = '\0';
				for(i = 0; i < t->ns; i++) {
					if(i)
						strcat(dev->mtd_name, " ");
					strcat(dev->mtd_name, t->str+t->ofs[i]);
				}
			}
			DEBUG(2, "Found name: %s", dev->mtd_name);
			break;
		}
			
		case CISTPL_JEDEC_C: {
			cistpl_jedec_t *t = &parse.jedec;
			int i;
			for(i = 0; i < t->nid; i++) {
				DEBUG(2, "JEDEC: 0x%02x 0x%02x", t->id[i].mfr, t->id[i].info);
			}
			break;
		}
			
		case CISTPL_DEVICE_GEO: {
			cistpl_device_geo_t *t = &parse.device_geo;
			int i;
			dev->pcmcia_map.bankwidth = t->geo[0].buswidth;
			for(i = 0; i < t->ngeo; i++) {
				DEBUG(2, "region: %d bankwidth = %u", i, t->geo[i].buswidth);
				DEBUG(2, "region: %d erase_block = %u", i, t->geo[i].erase_block);
				DEBUG(2, "region: %d read_block = %u", i, t->geo[i].read_block);
				DEBUG(2, "region: %d write_block = %u", i, t->geo[i].write_block);
				DEBUG(2, "region: %d partition = %u", i, t->geo[i].partition);
				DEBUG(2, "region: %d interleave = %u", i, t->geo[i].interleave);
			}
			break;
		}
			
		default:
			DEBUG(2, "Unknown tuple code %d", tuple.TupleCode);
		}
		
		rc = pcmcia_get_next_tuple(link->handle, &tuple);
	}
	if(!dev->pcmcia_map.size)
		dev->pcmcia_map.size = MAX_PCMCIA_ADDR;

	if(!dev->pcmcia_map.bankwidth)
		dev->pcmcia_map.bankwidth = 2;

	if(force_size) {
		dev->pcmcia_map.size = force_size << 20;
		DEBUG(2, "size forced to %dM", force_size);
	}

	if(bankwidth) {
		dev->pcmcia_map.bankwidth = bankwidth;
		DEBUG(2, "bankwidth forced to %d", bankwidth);
	}		

	dev->pcmcia_map.name = dev->mtd_name;
	if(!dev->mtd_name[0]) {
		strcpy(dev->mtd_name, "PCMCIA Memory card");
		*new_name = 1;
	}

	DEBUG(1, "Device: Size: %lu Width:%d Name: %s",
	      dev->pcmcia_map.size, dev->pcmcia_map.bankwidth << 3, dev->mtd_name);
}


/* pcmciamtd_config() is scheduled to run after a CARD_INSERTION event
 * is received, to configure the PCMCIA socket, and to make the
 * MTD device available to the system.
 */

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static void pcmciamtd_config(dev_link_t *link)
{
	struct pcmciamtd_dev *dev = link->priv;
	struct mtd_info *mtd = NULL;
	cs_status_t status;
	win_req_t req;
	int last_ret = 0, last_fn = 0;
	int ret;
	int i;
	config_info_t t;
	static char *probes[] = { "jedec_probe", "cfi_probe" };
	cisinfo_t cisinfo;
	int new_name = 0;

	DEBUG(3, "link=0x%p", link);

	/* Configure card */
	link->state |= DEV_CONFIG;

	DEBUG(2, "Validating CIS");
	ret = pcmcia_validate_cis(link->handle, &cisinfo);
	if(ret != CS_SUCCESS) {
		cs_error(link->handle, GetTupleData, ret);
	} else {
		DEBUG(2, "ValidateCIS found %d chains", cisinfo.Chains);
	}

	card_settings(dev, link, &new_name);

	dev->pcmcia_map.phys = NO_XIP;
	dev->pcmcia_map.copy_from = pcmcia_copy_from_remap;
	dev->pcmcia_map.copy_to = pcmcia_copy_to_remap;
	if (dev->pcmcia_map.bankwidth == 1) {
		dev->pcmcia_map.read = pcmcia_read8_remap;
		dev->pcmcia_map.write = pcmcia_write8_remap;
	} else {
		dev->pcmcia_map.read = pcmcia_read16_remap;
		dev->pcmcia_map.write = pcmcia_write16_remap;
	}
	if(setvpp == 1)
		dev->pcmcia_map.set_vpp = pcmciamtd_set_vpp;

	/* Request a memory window for PCMCIA. Some architeures can map windows upto the maximum
	   that PCMCIA can support (64MiB) - this is ideal and we aim for a window the size of the
	   whole card - otherwise we try smaller windows until we succeed */

	req.Attributes =  WIN_MEMORY_TYPE_CM | WIN_ENABLE;
	req.Attributes |= (dev->pcmcia_map.bankwidth == 1) ? WIN_DATA_WIDTH_8 : WIN_DATA_WIDTH_16;
	req.Base = 0;
	req.AccessSpeed = mem_speed;
	link->win = (window_handle_t)link->handle;
	req.Size = (force_size) ? force_size << 20 : MAX_PCMCIA_ADDR;
	dev->win_size = 0;

	do {
		int ret;
		DEBUG(2, "requesting window with size = %dKiB memspeed = %d",
		      req.Size >> 10, req.AccessSpeed);
		ret = pcmcia_request_window(&link->handle, &req, &link->win);
		DEBUG(2, "ret = %d dev->win_size = %d", ret, dev->win_size);
		if(ret) {
			req.Size >>= 1;
		} else {
			DEBUG(2, "Got window of size %dKiB", req.Size >> 10);
			dev->win_size = req.Size;
			break;
		}
	} while(req.Size >= 0x1000);

	DEBUG(2, "dev->win_size = %d", dev->win_size);

	if(!dev->win_size) {
		err("Cant allocate memory window");
		pcmciamtd_release(link);
		return;
	}
	DEBUG(1, "Allocated a window of %dKiB", dev->win_size >> 10);
		
	/* Get write protect status */
	CS_CHECK(GetStatus, pcmcia_get_status(link->handle, &status));
	DEBUG(2, "status value: 0x%x window handle = 0x%8.8lx",
	      status.CardState, (unsigned long)link->win);
	dev->win_base = ioremap(req.Base, req.Size);
	if(!dev->win_base) {
		err("ioremap(%lu, %u) failed", req.Base, req.Size);
		pcmciamtd_release(link);
		return;
	}
	DEBUG(1, "mapped window dev = %p req.base = 0x%lx base = %p size = 0x%x",
	      dev, req.Base, dev->win_base, req.Size);

	dev->offset = 0;
	dev->pcmcia_map.map_priv_1 = (unsigned long)dev;
	dev->pcmcia_map.map_priv_2 = (unsigned long)link->win;

	DEBUG(2, "Getting configuration");
	CS_CHECK(GetConfigurationInfo, pcmcia_get_configuration_info(link->handle, &t));
	DEBUG(2, "Vcc = %d Vpp1 = %d Vpp2 = %d", t.Vcc, t.Vpp1, t.Vpp2);
	dev->vpp = (vpp) ? vpp : t.Vpp1;
	link->conf.Attributes = 0;
	link->conf.Vcc = t.Vcc;
	if(setvpp == 2) {
		link->conf.Vpp1 = dev->vpp;
		link->conf.Vpp2 = dev->vpp;
	} else {
		link->conf.Vpp1 = 0;
		link->conf.Vpp2 = 0;
	}

	link->conf.IntType = INT_MEMORY;
	link->conf.ConfigBase = t.ConfigBase;
	link->conf.Status = t.Status;
	link->conf.Pin = t.Pin;
	link->conf.Copy = t.Copy;
	link->conf.ExtStatus = t.ExtStatus;
	link->conf.ConfigIndex = 0;
	link->conf.Present = t.Present;
	DEBUG(2, "Setting Configuration");
	ret = pcmcia_request_configuration(link->handle, &link->conf);
	if(ret != CS_SUCCESS) {
		cs_error(link->handle, RequestConfiguration, ret);
	}

	if(mem_type == 1) {
		mtd = do_map_probe("map_ram", &dev->pcmcia_map);
	} else if(mem_type == 2) {
		mtd = do_map_probe("map_rom", &dev->pcmcia_map);
	} else {
		for(i = 0; i < sizeof(probes) / sizeof(char *); i++) {
			DEBUG(1, "Trying %s", probes[i]);
			mtd = do_map_probe(probes[i], &dev->pcmcia_map);
			if(mtd)
				break;
			
			DEBUG(1, "FAILED: %s", probes[i]);
		}
	}
	
	if(!mtd) {
		DEBUG(1, "Cant find an MTD");
		pcmciamtd_release(link);
		return;
	}

	dev->mtd_info = mtd;
	mtd->owner = THIS_MODULE;

	if(new_name) {
		int size = 0;
		char unit = ' ';
		/* Since we are using a default name, make it better by adding in the
		   size */
		if(mtd->size < 1048576) { /* <1MiB in size, show size in KiB */
			size = mtd->size >> 10;
			unit = 'K';
		} else {
			size = mtd->size >> 20;
			unit = 'M';
		}
		snprintf(dev->mtd_name, sizeof(dev->mtd_name), "%d%ciB %s", size, unit, "PCMCIA Memory card");
	}

	/* If the memory found is fits completely into the mapped PCMCIA window,
	   use the faster non-remapping read/write functions */
	if(mtd->size <= dev->win_size) {
		DEBUG(1, "Using non remapping memory functions");
		dev->pcmcia_map.map_priv_1 = (unsigned long)&(dev->link.state);
		dev->pcmcia_map.map_priv_2 = (unsigned long)dev->win_base;
		if (dev->pcmcia_map.bankwidth == 1) {
			dev->pcmcia_map.read = pcmcia_read8;
			dev->pcmcia_map.write = pcmcia_write8;
		} else {
			dev->pcmcia_map.read = pcmcia_read16;
			dev->pcmcia_map.write = pcmcia_write16;
		}
		dev->pcmcia_map.copy_from = pcmcia_copy_from;
		dev->pcmcia_map.copy_to = pcmcia_copy_to;
	}

	if(add_mtd_device(mtd)) {
		map_destroy(mtd);
		dev->mtd_info = NULL;
		err("Couldnt register MTD device");
		pcmciamtd_release(link);
		return;
	}
	snprintf(dev->node.dev_name, sizeof(dev->node.dev_name), "mtd%d", mtd->index);
	info("mtd%d: %s", mtd->index, mtd->name);
	link->state &= ~DEV_CONFIG_PENDING;
	link->dev = &dev->node;
	return;

 cs_failed:
	cs_error(link->handle, last_fn, last_ret);
	err("CS Error, exiting");
	pcmciamtd_release(link);
	return;
}


/* The card status event handler.  Mostly, this schedules other
 * stuff to run after an event is received.  A CARD_REMOVAL event
 * also sets some flags to discourage the driver from trying
 * to talk to the card any more.
 */

static int pcmciamtd_event(event_t event, int priority,
			event_callback_args_t *args)
{
	dev_link_t *link = args->client_data;

	DEBUG(1, "event=0x%06x", event);
	switch (event) {
	case CS_EVENT_CARD_REMOVAL:
		DEBUG(2, "EVENT_CARD_REMOVAL");
		link->state &= ~DEV_PRESENT;
		if (link->state & DEV_CONFIG) {
			struct pcmciamtd_dev *dev = link->priv;
			if(dev->mtd_info) {
				del_mtd_device(dev->mtd_info);
				info("mtd%d: Removed", dev->mtd_info->index);
			}
			pcmciamtd_release(link);
		}
		break;
	case CS_EVENT_CARD_INSERTION:
		DEBUG(2, "EVENT_CARD_INSERTION");
		link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
		pcmciamtd_config(link);
		break;
	case CS_EVENT_PM_SUSPEND:
		DEBUG(2, "EVENT_PM_SUSPEND");
		link->state |= DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_RESET_PHYSICAL:
		DEBUG(2, "EVENT_RESET_PHYSICAL");
		/* get_lock(link); */
		break;
	case CS_EVENT_PM_RESUME:
		DEBUG(2, "EVENT_PM_RESUME");
		link->state &= ~DEV_SUSPEND;
		/* Fall through... */
	case CS_EVENT_CARD_RESET:
		DEBUG(2, "EVENT_CARD_RESET");
		/* free_lock(link); */
		break;
	default:
		DEBUG(2, "Unknown event %d", event);
	}
	return 0;
}


/* This deletes a driver "instance".  The device is de-registered
 * with Card Services.  If it has been released, all local data
 * structures are freed.  Otherwise, the structures will be freed
 * when the device is released.
 */

static void pcmciamtd_detach(dev_link_t *link)
{
	DEBUG(3, "link=0x%p", link);

	if(link->state & DEV_CONFIG) {
		pcmciamtd_release(link);
	}

	if (link->handle) {
		int ret;
		DEBUG(2, "Deregistering with card services");
		ret = pcmcia_deregister_client(link->handle);
		if (ret != CS_SUCCESS)
			cs_error(link->handle, DeregisterClient, ret);
	}

	link->state |= DEV_STALE_LINK;
}


/* pcmciamtd_attach() creates an "instance" of the driver, allocating
 * local data structures for one device.  The device is registered
 * with Card Services.
 */

static dev_link_t *pcmciamtd_attach(void)
{
	struct pcmciamtd_dev *dev;
	dev_link_t *link;
	client_reg_t client_reg;
	int ret;

	/* Create new memory card device */
	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) return NULL;
	DEBUG(1, "dev=0x%p", dev);

	memset(dev, 0, sizeof(*dev));
	link = &dev->link;
	link->priv = dev;

	link->conf.Attributes = 0;
	link->conf.IntType = INT_MEMORY;

	link->next = dev_list;
	dev_list = link;

	/* Register with Card Services */
	client_reg.dev_info = &dev_info;
	client_reg.EventMask =
		CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
		CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
		CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
	client_reg.event_handler = &pcmciamtd_event;
	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = link;
	DEBUG(2, "Calling RegisterClient");
	ret = pcmcia_register_client(&link->handle, &client_reg);
	if (ret != 0) {
		cs_error(link->handle, RegisterClient, ret);
		pcmciamtd_detach(link);
		return NULL;
	}
	DEBUG(2, "link = %p", link);
	return link;
}


static struct pcmcia_driver pcmciamtd_driver = {
	.drv		= {
		.name	= "pcmciamtd"
	},
	.attach		= pcmciamtd_attach,
	.detach		= pcmciamtd_detach,
	.owner		= THIS_MODULE
};


static int __init init_pcmciamtd(void)
{
	info(DRIVER_DESC " " DRIVER_VERSION);

	if(bankwidth && bankwidth != 1 && bankwidth != 2) {
		info("bad bankwidth (%d), using default", bankwidth);
		bankwidth = 2;
	}
	if(force_size && (force_size < 1 || force_size > 64)) {
		info("bad force_size (%d), using default", force_size);
		force_size = 0;
	}
	if(mem_type && mem_type != 1 && mem_type != 2) {
		info("bad mem_type (%d), using default", mem_type);
		mem_type = 0;
	}
	return pcmcia_register_driver(&pcmciamtd_driver);
}


static void __exit exit_pcmciamtd(void)
{
	DEBUG(1, DRIVER_DESC " unloading");
	pcmcia_unregister_driver(&pcmciamtd_driver);
	BUG_ON(dev_list != NULL);
}

module_init(init_pcmciamtd);
module_exit(exit_pcmciamtd);
