/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2009 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/div64.h>

#include "saa7164.h"

MODULE_DESCRIPTION("Driver for NXP SAA7164 based TV cards");
MODULE_AUTHOR("Steven Toth <stoth@kernellabs.com>");
MODULE_LICENSE("GPL");

/*
  1 Basic
  2
  4 i2c
  8 api
 16 cmd
 32 bus
 */

unsigned int saa_debug;
module_param_named(debug, saa_debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");

unsigned int waitsecs = 10;
module_param(waitsecs, int, 0644);
MODULE_PARM_DESC(debug, "timeout on firmware messages");

static unsigned int card[]  = {[0 ... (SAA7164_MAXBOARDS - 1)] = UNSET };
module_param_array(card,  int, NULL, 0444);
MODULE_PARM_DESC(card, "card type");

static unsigned int saa7164_devcount;

static DEFINE_MUTEX(devlist);
LIST_HEAD(saa7164_devlist);

#define INT_SIZE 16

static void saa7164_work_cmdhandler(struct work_struct *w)
{
	struct saa7164_dev *dev = container_of(w, struct saa7164_dev, workcmd);

	/* Wake up any complete commands */
	saa7164_irq_dequeue(dev);
}

static void saa7164_buffer_deliver(struct saa7164_buffer *buf)
{
	struct saa7164_tsport *port = buf->port;

	/* Feed the transport payload into the kernel demux */
	dvb_dmx_swfilter_packets(&port->dvb.demux, (u8 *)buf->cpu,
		SAA7164_TS_NUMBER_OF_LINES);

}

static irqreturn_t saa7164_irq_ts(struct saa7164_tsport *port)
{
	struct saa7164_dev *dev = port->dev;
	struct saa7164_buffer *buf;
	struct list_head *c, *n;
	int wp, i = 0, rp;

	/* Find the current write point from the hardware */
	wp = saa7164_readl(port->bufcounter);
	if (wp > (port->hwcfg.buffercount - 1))
		BUG();

	/* Find the previous buffer to the current write point */
	if (wp == 0)
		rp = 7;
	else
		rp = wp - 1;

	/* Lookup the WP in the buffer list */
	/* TODO: turn this into a worker thread */
	list_for_each_safe(c, n, &port->dmaqueue.list) {
		buf = list_entry(c, struct saa7164_buffer, list);
		if (i++ > port->hwcfg.buffercount)
			BUG();

		if (buf->nr == rp) {
			/* Found the buffer, deal with it */
			dprintk(DBGLVL_IRQ, "%s() wp: %d processing: %d\n",
				__func__, wp, rp);
			saa7164_buffer_deliver(buf);
			break;
		}

	}
	return 0;
}

/* Primary IRQ handler and dispatch mechanism */
static irqreturn_t saa7164_irq(int irq, void *dev_id)
{
	struct saa7164_dev *dev = dev_id;
	u32 intid, intstat[INT_SIZE/4];
	int i, handled = 0, bit;

	if (dev == 0) {
		printk(KERN_ERR "%s() No device specified\n", __func__);
		handled = 0;
		goto out;
	}

	/* Check that the hardware is accessable. If the status bytes are
	 * 0xFF then the device is not accessable, the the IRQ belongs
	 * to another driver.
	 * 4 x u32 interrupt registers.
	 */
	for (i = 0; i < INT_SIZE/4; i++) {

		/* TODO: Convert into saa7164_readl() */
		/* Read the 4 hardware interrupt registers */
		intstat[i] = saa7164_readl(dev->int_status + (i * 4));

		if (intstat[i])
			handled = 1;
	}
	if (handled == 0)
		goto out;

	/* For each of the HW interrupt registers */
	for (i = 0; i < INT_SIZE/4; i++) {

		if (intstat[i]) {
			/* Each function of the board has it's own interruptid.
			 * Find the function that triggered then call
			 * it's handler.
			 */
			for (bit = 0; bit < 32; bit++) {

				if (((intstat[i] >> bit) & 0x00000001) == 0)
					continue;

				/* Calculate the interrupt id (0x00 to 0x7f) */

				intid = (i * 32) + bit;
				if (intid == dev->intfdesc.bInterruptId) {
					/* A response to an cmd/api call */
					schedule_work(&dev->workcmd);
				} else if (intid ==
					dev->ts1.hwcfg.interruptid) {

					/* Transport path 1 */
					saa7164_irq_ts(&dev->ts1);

				} else if (intid ==
					dev->ts2.hwcfg.interruptid) {

					/* Transport path 2 */
					saa7164_irq_ts(&dev->ts2);

				} else {
					/* Find the function */
					dprintk(DBGLVL_IRQ,
						"%s() unhandled interrupt "
						"reg 0x%x bit 0x%x "
						"intid = 0x%x\n",
						__func__, i, bit, intid);
				}
			}

			/* Ack it */
			saa7164_writel(dev->int_ack + (i * 4), intstat[i]);

		}
	}
out:
	return IRQ_RETVAL(handled);
}

void saa7164_getfirmwarestatus(struct saa7164_dev *dev)
{
	struct saa7164_fw_status *s = &dev->fw_status;

	dev->fw_status.status = saa7164_readl(SAA_DEVICE_SYSINIT_STATUS);
	dev->fw_status.mode = saa7164_readl(SAA_DEVICE_SYSINIT_MODE);
	dev->fw_status.spec = saa7164_readl(SAA_DEVICE_SYSINIT_SPEC);
	dev->fw_status.inst = saa7164_readl(SAA_DEVICE_SYSINIT_INST);
	dev->fw_status.cpuload = saa7164_readl(SAA_DEVICE_SYSINIT_CPULOAD);
	dev->fw_status.remainheap =
		saa7164_readl(SAA_DEVICE_SYSINIT_REMAINHEAP);

	dprintk(1, "Firmware status:\n");
	dprintk(1, " .status     = 0x%08x\n", s->status);
	dprintk(1, " .mode       = 0x%08x\n", s->mode);
	dprintk(1, " .spec       = 0x%08x\n", s->spec);
	dprintk(1, " .inst       = 0x%08x\n", s->inst);
	dprintk(1, " .cpuload    = 0x%08x\n", s->cpuload);
	dprintk(1, " .remainheap = 0x%08x\n", s->remainheap);
}

u32 saa7164_getcurrentfirmwareversion(struct saa7164_dev *dev)
{
	u32 reg;

	reg = saa7164_readl(SAA_DEVICE_VERSION);
	dprintk(1, "Device running firmware version %d.%d.%d.%d (0x%x)\n",
		(reg & 0x0000fc00) >> 10,
		(reg & 0x000003e0) >> 5,
		(reg & 0x0000001f),
		(reg & 0xffff0000) >> 16,
		reg);

	return reg;
}

/* TODO: Debugging func, remove */
void saa7164_dumphex16(struct saa7164_dev *dev, u8 *buf, int len)
{
	int i;

	printk(KERN_INFO "--------------------> "
		"00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n");

	for (i = 0; i < len; i += 16)
		printk(KERN_INFO "         [0x%08x] "
			"%02x %02x %02x %02x %02x %02x %02x %02x "
			"%02x %02x %02x %02x %02x %02x %02x %02x\n", i,
		*(buf+i+0), *(buf+i+1), *(buf+i+2), *(buf+i+3),
		*(buf+i+4), *(buf+i+5), *(buf+i+6), *(buf+i+7),
		*(buf+i+8), *(buf+i+9), *(buf+i+10), *(buf+i+11),
		*(buf+i+12), *(buf+i+13), *(buf+i+14), *(buf+i+15));
}

/* TODO: Debugging func, remove */
void saa7164_dumpregs(struct saa7164_dev *dev, u32 addr)
{
	int i;

	dprintk(1, "--------------------> "
		"00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n");

	for (i = 0; i < 0x100; i += 16)
		dprintk(1, "region0[0x%08x] = "
			"%02x %02x %02x %02x %02x %02x %02x %02x"
			" %02x %02x %02x %02x %02x %02x %02x %02x\n", i,
			(u8)saa7164_readb(addr + i + 0),
			(u8)saa7164_readb(addr + i + 1),
			(u8)saa7164_readb(addr + i + 2),
			(u8)saa7164_readb(addr + i + 3),
			(u8)saa7164_readb(addr + i + 4),
			(u8)saa7164_readb(addr + i + 5),
			(u8)saa7164_readb(addr + i + 6),
			(u8)saa7164_readb(addr + i + 7),
			(u8)saa7164_readb(addr + i + 8),
			(u8)saa7164_readb(addr + i + 9),
			(u8)saa7164_readb(addr + i + 10),
			(u8)saa7164_readb(addr + i + 11),
			(u8)saa7164_readb(addr + i + 12),
			(u8)saa7164_readb(addr + i + 13),
			(u8)saa7164_readb(addr + i + 14),
			(u8)saa7164_readb(addr + i + 15)
			);
}

static void saa7164_dump_hwdesc(struct saa7164_dev *dev)
{
	dprintk(1, "@0x%p hwdesc sizeof(tmComResHWDescr_t) = %d bytes\n",
		&dev->hwdesc, (u32)sizeof(tmComResHWDescr_t));

	dprintk(1, " .bLength = 0x%x\n", dev->hwdesc.bLength);
	dprintk(1, " .bDescriptorType = 0x%x\n", dev->hwdesc.bDescriptorType);
	dprintk(1, " .bDescriptorSubtype = 0x%x\n",
		dev->hwdesc.bDescriptorSubtype);

	dprintk(1, " .bcdSpecVersion = 0x%x\n", dev->hwdesc.bcdSpecVersion);
	dprintk(1, " .dwClockFrequency = 0x%x\n", dev->hwdesc.dwClockFrequency);
	dprintk(1, " .dwClockUpdateRes = 0x%x\n", dev->hwdesc.dwClockUpdateRes);
	dprintk(1, " .bCapabilities = 0x%x\n", dev->hwdesc.bCapabilities);
	dprintk(1, " .dwDeviceRegistersLocation = 0x%x\n",
		dev->hwdesc.dwDeviceRegistersLocation);

	dprintk(1, " .dwHostMemoryRegion = 0x%x\n",
		dev->hwdesc.dwHostMemoryRegion);

	dprintk(1, " .dwHostMemoryRegionSize = 0x%x\n",
		dev->hwdesc.dwHostMemoryRegionSize);

	dprintk(1, " .dwHostHibernatMemRegion = 0x%x\n",
		dev->hwdesc.dwHostHibernatMemRegion);

	dprintk(1, " .dwHostHibernatMemRegionSize = 0x%x\n",
		dev->hwdesc.dwHostHibernatMemRegionSize);
}

static void saa7164_dump_intfdesc(struct saa7164_dev *dev)
{
	dprintk(1, "@0x%p intfdesc "
		"sizeof(tmComResInterfaceDescr_t) = %d bytes\n",
		&dev->intfdesc, (u32)sizeof(tmComResInterfaceDescr_t));

	dprintk(1, " .bLength = 0x%x\n", dev->intfdesc.bLength);
	dprintk(1, " .bDescriptorType = 0x%x\n", dev->intfdesc.bDescriptorType);
	dprintk(1, " .bDescriptorSubtype = 0x%x\n",
		dev->intfdesc.bDescriptorSubtype);

	dprintk(1, " .bFlags = 0x%x\n", dev->intfdesc.bFlags);
	dprintk(1, " .bInterfaceType = 0x%x\n", dev->intfdesc.bInterfaceType);
	dprintk(1, " .bInterfaceId = 0x%x\n", dev->intfdesc.bInterfaceId);
	dprintk(1, " .bBaseInterface = 0x%x\n", dev->intfdesc.bBaseInterface);
	dprintk(1, " .bInterruptId = 0x%x\n", dev->intfdesc.bInterruptId);
	dprintk(1, " .bDebugInterruptId = 0x%x\n",
		dev->intfdesc.bDebugInterruptId);

	dprintk(1, " .BARLocation = 0x%x\n", dev->intfdesc.BARLocation);
}

static void saa7164_dump_busdesc(struct saa7164_dev *dev)
{
	dprintk(1, "@0x%p busdesc sizeof(tmComResBusDescr_t) = %d bytes\n",
		&dev->busdesc, (u32)sizeof(tmComResBusDescr_t));

	dprintk(1, " .CommandRing   = 0x%016Lx\n", dev->busdesc.CommandRing);
	dprintk(1, " .ResponseRing  = 0x%016Lx\n", dev->busdesc.ResponseRing);
	dprintk(1, " .CommandWrite  = 0x%x\n", dev->busdesc.CommandWrite);
	dprintk(1, " .CommandRead   = 0x%x\n", dev->busdesc.CommandRead);
	dprintk(1, " .ResponseWrite = 0x%x\n", dev->busdesc.ResponseWrite);
	dprintk(1, " .ResponseRead  = 0x%x\n", dev->busdesc.ResponseRead);
}

/* Much of the hardware configuration and PCI registers are configured
 * dynamically depending on firmware. We have to cache some initial
 * structures then use these to locate other important structures
 * from PCI space.
 */
static void saa7164_get_descriptors(struct saa7164_dev *dev)
{
	memcpy(&dev->hwdesc, dev->bmmio, sizeof(tmComResHWDescr_t));
	memcpy(&dev->intfdesc, dev->bmmio + sizeof(tmComResHWDescr_t),
		sizeof(tmComResInterfaceDescr_t));
	memcpy(&dev->busdesc, dev->bmmio + dev->intfdesc.BARLocation,
		sizeof(tmComResBusDescr_t));

	if (dev->hwdesc.bLength != sizeof(tmComResHWDescr_t)) {
		printk(KERN_ERR "Structure tmComResHWDescr_t is mangled\n");
		printk(KERN_ERR "Need %x got %d\n", dev->hwdesc.bLength,
			(u32)sizeof(tmComResHWDescr_t));
	} else
		saa7164_dump_hwdesc(dev);

	if (dev->intfdesc.bLength != sizeof(tmComResInterfaceDescr_t)) {
		printk(KERN_ERR "struct tmComResInterfaceDescr_t is mangled\n");
		printk(KERN_ERR "Need %x got %d\n", dev->intfdesc.bLength,
			(u32)sizeof(tmComResInterfaceDescr_t));
	} else
		saa7164_dump_intfdesc(dev);

	saa7164_dump_busdesc(dev);
}

static int saa7164_pci_quirks(struct saa7164_dev *dev)
{
	return 0;
}

static int get_resources(struct saa7164_dev *dev)
{
	if (request_mem_region(pci_resource_start(dev->pci, 0),
		pci_resource_len(dev->pci, 0), dev->name)) {

		if (request_mem_region(pci_resource_start(dev->pci, 2),
			pci_resource_len(dev->pci, 2), dev->name))
			return 0;
	}

	printk(KERN_ERR "%s: can't get MMIO memory @ 0x%llx or 0x%llx\n",
		dev->name,
		(u64)pci_resource_start(dev->pci, 0),
		(u64)pci_resource_start(dev->pci, 2));

	return -EBUSY;
}

static int saa7164_dev_setup(struct saa7164_dev *dev)
{
	int i;

	mutex_init(&dev->lock);
	atomic_inc(&dev->refcount);
	dev->nr = saa7164_devcount++;

	sprintf(dev->name, "saa7164[%d]", dev->nr);

	mutex_lock(&devlist);
	list_add_tail(&dev->devlist, &saa7164_devlist);
	mutex_unlock(&devlist);

	/* board config */
	dev->board = UNSET;
	if (card[dev->nr] < saa7164_bcount)
		dev->board = card[dev->nr];

	for (i = 0; UNSET == dev->board  &&  i < saa7164_idcount; i++)
		if (dev->pci->subsystem_vendor == saa7164_subids[i].subvendor &&
			dev->pci->subsystem_device ==
				saa7164_subids[i].subdevice)
				dev->board = saa7164_subids[i].card;

	if (UNSET == dev->board) {
		dev->board = SAA7164_BOARD_UNKNOWN;
		saa7164_card_list(dev);
	}

	dev->pci_bus  = dev->pci->bus->number;
	dev->pci_slot = PCI_SLOT(dev->pci->devfn);

	/* I2C Defaults / setup */
	dev->i2c_bus[0].dev = dev;
	dev->i2c_bus[0].nr = 0;
	dev->i2c_bus[1].dev = dev;
	dev->i2c_bus[1].nr = 1;
	dev->i2c_bus[2].dev = dev;
	dev->i2c_bus[2].nr = 2;

	/* Transport port A Defaults / setup */
	dev->ts1.dev = dev;
	dev->ts1.nr = 0;
	mutex_init(&dev->ts1.dvb.lock);
	INIT_LIST_HEAD(&dev->ts1.dmaqueue.list);
	INIT_LIST_HEAD(&dev->ts1.dummy_dmaqueue.list);
	mutex_init(&dev->ts1.dmaqueue_lock);
	mutex_init(&dev->ts1.dummy_dmaqueue_lock);

	/* Transport port B Defaults / setup */
	dev->ts2.dev = dev;
	dev->ts2.nr = 1;
	mutex_init(&dev->ts2.dvb.lock);
	INIT_LIST_HEAD(&dev->ts2.dmaqueue.list);
	INIT_LIST_HEAD(&dev->ts2.dummy_dmaqueue.list);
	mutex_init(&dev->ts2.dmaqueue_lock);
	mutex_init(&dev->ts2.dummy_dmaqueue_lock);

	if (get_resources(dev) < 0) {
		printk(KERN_ERR "CORE %s No more PCIe resources for "
		       "subsystem: %04x:%04x\n",
		       dev->name, dev->pci->subsystem_vendor,
		       dev->pci->subsystem_device);

		saa7164_devcount--;
		return -ENODEV;
	}

	/* PCI/e allocations */
	dev->lmmio = ioremap(pci_resource_start(dev->pci, 0),
			     pci_resource_len(dev->pci, 0));

	dev->lmmio2 = ioremap(pci_resource_start(dev->pci, 2),
			     pci_resource_len(dev->pci, 2));

	dev->bmmio = (u8 __iomem *)dev->lmmio;
	dev->bmmio2 = (u8 __iomem *)dev->lmmio2;

	/* Inerrupt and ack register locations offset of bmmio */
	dev->int_status = 0x183000 + 0xf80;
	dev->int_ack = 0x183000 + 0xf90;

	printk(KERN_INFO
		"CORE %s: subsystem: %04x:%04x, board: %s [card=%d,%s]\n",
	       dev->name, dev->pci->subsystem_vendor,
	       dev->pci->subsystem_device, saa7164_boards[dev->board].name,
	       dev->board, card[dev->nr] == dev->board ?
	       "insmod option" : "autodetected");

	saa7164_pci_quirks(dev);

	return 0;
}

static void saa7164_dev_unregister(struct saa7164_dev *dev)
{
	dprintk(1, "%s()\n", __func__);

	release_mem_region(pci_resource_start(dev->pci, 0),
		pci_resource_len(dev->pci, 0));

	release_mem_region(pci_resource_start(dev->pci, 2),
		pci_resource_len(dev->pci, 2));

	if (!atomic_dec_and_test(&dev->refcount))
		return;

	iounmap(dev->lmmio);
	iounmap(dev->lmmio2);

	return;
}

static int __devinit saa7164_initdev(struct pci_dev *pci_dev,
				     const struct pci_device_id *pci_id)
{
	struct saa7164_dev *dev;
	int err, i;
	u32 version;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (NULL == dev)
		return -ENOMEM;

	/* pci init */
	dev->pci = pci_dev;
	if (pci_enable_device(pci_dev)) {
		err = -EIO;
		goto fail_free;
	}

	if (saa7164_dev_setup(dev) < 0) {
		err = -EINVAL;
		goto fail_free;
	}

	/* print pci info */
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &dev->pci_rev);
	pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER,  &dev->pci_lat);
	printk(KERN_INFO "%s/0: found at %s, rev: %d, irq: %d, "
	       "latency: %d, mmio: 0x%llx\n", dev->name,
	       pci_name(pci_dev), dev->pci_rev, pci_dev->irq,
	       dev->pci_lat,
		(unsigned long long)pci_resource_start(pci_dev, 0));

	pci_set_master(pci_dev);
	/* TODO */
	if (!pci_dma_supported(pci_dev, 0xffffffff)) {
		printk("%s/0: Oops: no 32bit PCI DMA ???\n", dev->name);
		err = -EIO;
		goto fail_irq;
	}

	err = request_irq(pci_dev->irq, saa7164_irq,
		IRQF_SHARED | IRQF_DISABLED, dev->name, dev);
	if (err < 0) {
		printk(KERN_ERR "%s: can't get IRQ %d\n", dev->name,
			pci_dev->irq);
		err = -EIO;
		goto fail_irq;
	}

	pci_set_drvdata(pci_dev, dev);

	/* Init the internal command list */
	for (i = 0; i < SAA_CMD_MAX_MSG_UNITS; i++) {
		dev->cmds[i].seqno = i;
		dev->cmds[i].inuse = 0;
		mutex_init(&dev->cmds[i].lock);
		init_waitqueue_head(&dev->cmds[i].wait);
	}

	/* We need a deferred interrupt handler for cmd handling */
	INIT_WORK(&dev->workcmd, saa7164_work_cmdhandler);

	/* Only load the firmware if we know the board */
	if (dev->board != SAA7164_BOARD_UNKNOWN) {

		err = saa7164_downloadfirmware(dev);
		if (err < 0) {
			printk(KERN_ERR
				"Failed to boot firmware, no features "
				"registered\n");
			goto fail_fw;
		}

		saa7164_get_descriptors(dev);
		saa7164_dumpregs(dev, 0);
		saa7164_getcurrentfirmwareversion(dev);
		saa7164_getfirmwarestatus(dev);
		err = saa7164_bus_setup(dev);
		if (err < 0)
			printk(KERN_ERR
				"Failed to setup the bus, will continue\n");
		saa7164_bus_dump(dev);

		/* Ping the running firmware via the command bus and get the
		 * firmware version, this checks the bus is running OK.
		 */
		version = 0;
		if (saa7164_api_get_fw_version(dev, &version) == SAA_OK)
			dprintk(1, "Bus is operating correctly using "
				"version %d.%d.%d.%d (0x%x)\n",
				(version & 0x0000fc00) >> 10,
				(version & 0x000003e0) >> 5,
				(version & 0x0000001f),
				(version & 0xffff0000) >> 16,
				version);
		else
			printk(KERN_ERR
				"Failed to communicate with the firmware\n");

		/* Bring up the I2C buses */
		saa7164_i2c_register(&dev->i2c_bus[0]);
		saa7164_i2c_register(&dev->i2c_bus[1]);
		saa7164_i2c_register(&dev->i2c_bus[2]);
		saa7164_gpio_setup(dev);
		saa7164_card_setup(dev);


		/* Parse the dynamic device configuration, find various
		 * media endpoints (MPEG, WMV, PS, TS) and cache their
		 * configuration details into the driver, so we can
		 * reference them later during simething_register() func,
		 * interrupt handlers, deferred work handlers etc.
		 */
		saa7164_api_enum_subdevs(dev);

		/* Begin to create the video sub-systems and register funcs */
		if (saa7164_boards[dev->board].porta == SAA7164_MPEG_DVB) {
			if (saa7164_dvb_register(&dev->ts1) < 0) {
				printk(KERN_ERR "%s() Failed to register "
					"dvb adapters on porta\n",
					__func__);
			}
		}

		if (saa7164_boards[dev->board].portb == SAA7164_MPEG_DVB) {
			if (saa7164_dvb_register(&dev->ts2) < 0) {
				printk(KERN_ERR"%s() Failed to register "
					"dvb adapters on portb\n",
					__func__);
			}
		}

	} /* != BOARD_UNKNOWN */
	else
		printk(KERN_ERR "%s() Unsupported board detected, "
			"registering without firmware\n", __func__);

	dprintk(1, "%s() parameter debug = %d\n", __func__, saa_debug);
	dprintk(1, "%s() parameter waitsecs = %d\n", __func__, waitsecs);

fail_fw:
	return 0;

fail_irq:
	saa7164_dev_unregister(dev);
fail_free:
	kfree(dev);
	return err;
}

static void saa7164_shutdown(struct saa7164_dev *dev)
{
	dprintk(1, "%s()\n", __func__);
}

static void __devexit saa7164_finidev(struct pci_dev *pci_dev)
{
	struct saa7164_dev *dev = pci_get_drvdata(pci_dev);

	saa7164_shutdown(dev);

	if (saa7164_boards[dev->board].porta == SAA7164_MPEG_DVB)
		saa7164_dvb_unregister(&dev->ts1);

	if (saa7164_boards[dev->board].portb == SAA7164_MPEG_DVB)
		saa7164_dvb_unregister(&dev->ts2);

	saa7164_i2c_unregister(&dev->i2c_bus[0]);
	saa7164_i2c_unregister(&dev->i2c_bus[1]);
	saa7164_i2c_unregister(&dev->i2c_bus[2]);

	pci_disable_device(pci_dev);

	/* unregister stuff */
	free_irq(pci_dev->irq, dev);
	pci_set_drvdata(pci_dev, NULL);

	mutex_lock(&devlist);
	list_del(&dev->devlist);
	mutex_unlock(&devlist);

	saa7164_dev_unregister(dev);
	kfree(dev);
}

static struct pci_device_id saa7164_pci_tbl[] = {
	{
		/* SAA7164 */
		.vendor       = 0x1131,
		.device       = 0x7164,
		.subvendor    = PCI_ANY_ID,
		.subdevice    = PCI_ANY_ID,
	}, {
		/* --- end of list --- */
	}
};
MODULE_DEVICE_TABLE(pci, saa7164_pci_tbl);

static struct pci_driver saa7164_pci_driver = {
	.name     = "saa7164",
	.id_table = saa7164_pci_tbl,
	.probe    = saa7164_initdev,
	.remove   = __devexit_p(saa7164_finidev),
	/* TODO */
	.suspend  = NULL,
	.resume   = NULL,
};

static int saa7164_init(void)
{
	printk(KERN_INFO "saa7164 driver loaded\n");
	return pci_register_driver(&saa7164_pci_driver);
}

static void saa7164_fini(void)
{
	pci_unregister_driver(&saa7164_pci_driver);
}

module_init(saa7164_init);
module_exit(saa7164_fini);

