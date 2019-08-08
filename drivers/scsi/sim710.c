// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sim710.c - Copyright (C) 1999 Richard Hirst <richard@sleepie.demon.co.uk>
 *
 *----------------------------------------------------------------------------
 *----------------------------------------------------------------------------
 *
 * MCA card detection code by Trent McNair. (now deleted)
 * Fixes to not explicitly nul bss data from Xavier Bestel.
 * Some multiboard fixes from Rolf Eike Beer.
 * Auto probing of EISA config space from Trevor Hemsley.
 *
 * Rewritten to use 53c700.c by James.Bottomley@SteelEye.com
 */

#include <linux/module.h>
#include <linux/slab.h>

#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/eisa.h>
#include <linux/interrupt.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_spi.h>

#include "53c700.h"


/* Must be enough for EISA */
#define MAX_SLOTS 8
static __u8 __initdata id_array[MAX_SLOTS] = { [0 ... MAX_SLOTS-1] = 7 };

static char *sim710;		/* command line passed by insmod */

MODULE_AUTHOR("Richard Hirst");
MODULE_DESCRIPTION("Simple NCR53C710 driver");
MODULE_LICENSE("GPL");

module_param(sim710, charp, 0);

#ifdef MODULE
#define ARG_SEP ' '
#else
#define ARG_SEP ','
#endif

static __init int
param_setup(char *str)
{
	char *pos = str, *next;
	int slot = -1;

	while(pos != NULL && (next = strchr(pos, ':')) != NULL) {
		int val = (int)simple_strtoul(++next, NULL, 0);

		if(!strncmp(pos, "slot:", 5))
			slot = val;
		else if(!strncmp(pos, "id:", 3)) {
			if(slot == -1) {
				printk(KERN_WARNING "sim710: Must specify slot for id parameter\n");
			} else if(slot >= MAX_SLOTS) {
				printk(KERN_WARNING "sim710: Illegal slot %d for id %d\n", slot, val);
			} else {
				id_array[slot] = val;
			}
		}
		if((pos = strchr(pos, ARG_SEP)) != NULL)
			pos++;
	}
	return 1;
}
__setup("sim710=", param_setup);

static struct scsi_host_template sim710_driver_template = {
	.name			= "LSI (Symbios) 710 EISA",
	.proc_name		= "sim710",
	.this_id		= 7,
	.module			= THIS_MODULE,
};

static int sim710_probe_common(struct device *dev, unsigned long base_addr,
			       int irq, int clock, int differential,
			       int scsi_id)
{
	struct Scsi_Host * host = NULL;
	struct NCR_700_Host_Parameters *hostdata =
		kzalloc(sizeof(struct NCR_700_Host_Parameters),	GFP_KERNEL);

	printk(KERN_NOTICE "sim710: %s\n", dev_name(dev));
	printk(KERN_NOTICE "sim710: irq = %d, clock = %d, base = 0x%lx, scsi_id = %d\n",
	       irq, clock, base_addr, scsi_id);

	if(hostdata == NULL) {
		printk(KERN_ERR "sim710: Failed to allocate host data\n");
		goto out;
	}

	if(request_region(base_addr, 64, "sim710") == NULL) {
		printk(KERN_ERR "sim710: Failed to reserve IO region 0x%lx\n",
		       base_addr);
		goto out_free;
	}

	/* Fill in the three required pieces of hostdata */
	hostdata->base = ioport_map(base_addr, 64);
	hostdata->differential = differential;
	hostdata->clock = clock;
	hostdata->chip710 = 1;
	hostdata->burst_length = 8;

	/* and register the chip */
	if((host = NCR_700_detect(&sim710_driver_template, hostdata, dev))
	   == NULL) {
		printk(KERN_ERR "sim710: No host detected; card configuration problem?\n");
		goto out_release;
	}
	host->this_id = scsi_id;
	host->base = base_addr;
	host->irq = irq;
	if (request_irq(irq, NCR_700_intr, IRQF_SHARED, "sim710", host)) {
		printk(KERN_ERR "sim710: request_irq failed\n");
		goto out_put_host;
	}

	dev_set_drvdata(dev, host);
	scsi_scan_host(host);

	return 0;

 out_put_host:
	scsi_host_put(host);
 out_release:
	release_region(base_addr, 64);
 out_free:
	kfree(hostdata);
 out:
	return -ENODEV;
}

static int sim710_device_remove(struct device *dev)
{
	struct Scsi_Host *host = dev_get_drvdata(dev);
	struct NCR_700_Host_Parameters *hostdata =
		(struct NCR_700_Host_Parameters *)host->hostdata[0];

	scsi_remove_host(host);
	NCR_700_release(host);
	kfree(hostdata);
	free_irq(host->irq, host);
	release_region(host->base, 64);
	return 0;
}

#ifdef CONFIG_EISA
static struct eisa_device_id sim710_eisa_ids[] = {
	{ "CPQ4410" },
	{ "CPQ4411" },
	{ "HWP0C80" },
	{ "" }
};
MODULE_DEVICE_TABLE(eisa, sim710_eisa_ids);

static int sim710_eisa_probe(struct device *dev)
{
	struct eisa_device *edev = to_eisa_device(dev);
	unsigned long io_addr = edev->base_addr;
	char eisa_cpq_irqs[] = { 11, 14, 15, 10, 9, 0 };
	char eisa_hwp_irqs[] = { 3, 4, 5, 7, 12, 10, 11, 0};
	char *eisa_irqs;
	unsigned char irq_index;
	unsigned char irq, differential = 0, scsi_id = 7;

	if(strcmp(edev->id.sig, "HWP0C80") == 0) {
		__u8 val;
		eisa_irqs =  eisa_hwp_irqs;
		irq_index = (inb(io_addr + 0xc85) & 0x7) - 1;

		val = inb(io_addr + 0x4);
		scsi_id = ffs(val) - 1;

		if(scsi_id > 7 || (val & ~(1<<scsi_id)) != 0) {
			printk(KERN_ERR "sim710.c, EISA card %s has incorrect scsi_id, setting to 7\n", dev_name(dev));
			scsi_id = 7;
		}
	} else {
		eisa_irqs = eisa_cpq_irqs;
		irq_index = inb(io_addr + 0xc88) & 0x07;
	}

	if(irq_index >= strlen(eisa_irqs)) {
		printk("sim710.c: irq nasty\n");
		return -ENODEV;
	}

	irq = eisa_irqs[irq_index];
		
	return sim710_probe_common(dev, io_addr, irq, 50,
				   differential, scsi_id);
}

static struct eisa_driver sim710_eisa_driver = {
	.id_table		= sim710_eisa_ids,
	.driver = {
		.name		= "sim710",
		.probe		= sim710_eisa_probe,
		.remove		= sim710_device_remove,
	},
};
#endif /* CONFIG_EISA */

static int __init sim710_init(void)
{
	int err = -ENODEV;

#ifdef MODULE
	if (sim710)
		param_setup(sim710);
#endif

#ifdef CONFIG_EISA
	err = eisa_driver_register(&sim710_eisa_driver);
#endif
	/* FIXME: what we'd really like to return here is -ENODEV if
	 * no devices have actually been found.  Instead, the err
	 * above actually only reports problems with kobject_register,
	 * so for the moment return success */

	return 0;
}

static void __exit sim710_exit(void)
{
#ifdef CONFIG_EISA
	eisa_driver_unregister(&sim710_eisa_driver);
#endif
}

module_init(sim710_init);
module_exit(sim710_exit);
