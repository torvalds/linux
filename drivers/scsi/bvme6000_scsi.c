/*
 * Detection routine for the NCR53c710 based BVME6000 SCSI Controllers for Linux.
 *
 * Based on work by Alan Hourihane and Kars de Jong
 *
 * Rewritten to use 53c700.c by Richard Hirst <richard@sleepie.demon.co.uk>
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <asm/bvme6000hw.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_spi.h>

#include "53c700.h"

MODULE_AUTHOR("Richard Hirst <richard@sleepie.demon.co.uk>");
MODULE_DESCRIPTION("BVME6000 NCR53C710 driver");
MODULE_LICENSE("GPL");

static struct scsi_host_template bvme6000_scsi_driver_template = {
	.name			= "BVME6000 NCR53c710 SCSI",
	.proc_name		= "BVME6000",
	.this_id		= 7,
	.module			= THIS_MODULE,
};

static struct platform_device *bvme6000_scsi_device;

static int
bvme6000_probe(struct platform_device *dev)
{
	struct Scsi_Host *host;
	struct NCR_700_Host_Parameters *hostdata;

	if (!MACH_IS_BVME6000)
		goto out;

	hostdata = kzalloc(sizeof(struct NCR_700_Host_Parameters), GFP_KERNEL);
	if (!hostdata) {
		printk(KERN_ERR "bvme6000-scsi: "
				"Failed to allocate host data\n");
		goto out;
	}

	/* Fill in the required pieces of hostdata */
	hostdata->base = (void __iomem *)BVME_NCR53C710_BASE;
	hostdata->clock = 40;	/* XXX - depends on the CPU clock! */
	hostdata->chip710 = 1;
	hostdata->dmode_extra = DMODE_FC2;
	hostdata->dcntl_extra = EA_710;
	hostdata->ctest7_extra = CTEST7_TT1;

	/* and register the chip */
	host = NCR_700_detect(&bvme6000_scsi_driver_template, hostdata,
			      &dev->dev);
	if (!host) {
		printk(KERN_ERR "bvme6000-scsi: No host detected; "
				"board configuration problem?\n");
		goto out_free;
	}
	host->base = BVME_NCR53C710_BASE;
	host->this_id = 7;
	host->irq = BVME_IRQ_SCSI;
	if (request_irq(BVME_IRQ_SCSI, NCR_700_intr, 0, "bvme6000-scsi",
			host)) {
		printk(KERN_ERR "bvme6000-scsi: request_irq failed\n");
		goto out_put_host;
	}

	platform_set_drvdata(dev, host);
	scsi_scan_host(host);

	return 0;

 out_put_host:
	scsi_host_put(host);
 out_free:
	kfree(hostdata);
 out:
	return -ENODEV;
}

static int
bvme6000_device_remove(struct platform_device *dev)
{
	struct Scsi_Host *host = platform_get_drvdata(dev);
	struct NCR_700_Host_Parameters *hostdata = shost_priv(host);

	scsi_remove_host(host);
	NCR_700_release(host);
	kfree(hostdata);
	free_irq(host->irq, host);

	return 0;
}

static struct platform_driver bvme6000_scsi_driver = {
	.driver = {
		.name		= "bvme6000-scsi",
		.owner		= THIS_MODULE,
	},
	.probe		= bvme6000_probe,
	.remove		= bvme6000_device_remove,
};

static int __init bvme6000_scsi_init(void)
{
	int err;

	err = platform_driver_register(&bvme6000_scsi_driver);
	if (err)
		return err;

	bvme6000_scsi_device = platform_device_register_simple("bvme6000-scsi",
							       -1, NULL, 0);
	if (IS_ERR(bvme6000_scsi_device)) {
		platform_driver_unregister(&bvme6000_scsi_driver);
		return PTR_ERR(bvme6000_scsi_device);
	}

	return 0;
}

static void __exit bvme6000_scsi_exit(void)
{
	platform_device_unregister(bvme6000_scsi_device);
	platform_driver_unregister(&bvme6000_scsi_driver);
}

module_init(bvme6000_scsi_init);
module_exit(bvme6000_scsi_exit);
