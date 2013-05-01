/*
 * Detection routine for the NCR53c710 based Amiga SCSI Controllers for Linux.
 *		Amiga MacroSystemUS WarpEngine SCSI controller.
 *		Amiga Technologies/DKB A4091 SCSI controller.
 *
 * Written 1997 by Alan Hourihane <alanh@fairlite.demon.co.uk>
 * plus modifications of the 53c7xx.c driver to support the Amiga.
 *
 * Rewritten to use 53c700.c by Kars de Jong <jongk@linux-m68k.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/zorro.h>
#include <linux/slab.h>

#include <asm/amigahw.h>
#include <asm/amigaints.h>

#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_spi.h>

#include "53c700.h"

MODULE_AUTHOR("Alan Hourihane <alanh@fairlite.demon.co.uk> / Kars de Jong <jongk@linux-m68k.org>");
MODULE_DESCRIPTION("Amiga Zorro NCR53C710 driver");
MODULE_LICENSE("GPL");


static struct scsi_host_template zorro7xx_scsi_driver_template = {
	.proc_name	= "zorro7xx",
	.this_id	= 7,
	.module		= THIS_MODULE,
};

static struct zorro_driver_data {
	const char *name;
	unsigned long offset;
	int absolute;	/* offset is absolute address */
} zorro7xx_driver_data[] = {
	{ .name = "PowerUP 603e+", .offset = 0xf40000, .absolute = 1 },
	{ .name = "WarpEngine 40xx", .offset = 0x40000 },
	{ .name = "A4091", .offset = 0x800000 },
	{ .name = "GForce 040/060", .offset = 0x40000 },
	{ 0 }
};

static struct zorro_device_id zorro7xx_zorro_tbl[] = {
	{
		.id = ZORRO_PROD_PHASE5_BLIZZARD_603E_PLUS,
		.driver_data = (unsigned long)&zorro7xx_driver_data[0],
	},
	{
		.id = ZORRO_PROD_MACROSYSTEMS_WARP_ENGINE_40xx,
		.driver_data = (unsigned long)&zorro7xx_driver_data[1],
	},
	{
		.id = ZORRO_PROD_CBM_A4091_1,
		.driver_data = (unsigned long)&zorro7xx_driver_data[2],
	},
	{
		.id = ZORRO_PROD_CBM_A4091_2,
		.driver_data = (unsigned long)&zorro7xx_driver_data[2],
	},
	{
		.id = ZORRO_PROD_GVP_GFORCE_040_060,
		.driver_data = (unsigned long)&zorro7xx_driver_data[3],
	},
	{ 0 }
};
MODULE_DEVICE_TABLE(zorro, zorro7xx_zorro_tbl);

static int zorro7xx_init_one(struct zorro_dev *z,
			     const struct zorro_device_id *ent)
{
	struct Scsi_Host *host;
	struct NCR_700_Host_Parameters *hostdata;
	struct zorro_driver_data *zdd;
	unsigned long board, ioaddr;

	board = zorro_resource_start(z);
	zdd = (struct zorro_driver_data *)ent->driver_data;

	if (zdd->absolute) {
		ioaddr = zdd->offset;
	} else {
		ioaddr = board + zdd->offset;
	}

	if (!zorro_request_device(z, zdd->name)) {
		printk(KERN_ERR "zorro7xx: cannot reserve region 0x%lx, abort\n",
		       board);
		return -EBUSY;
	}

	hostdata = kzalloc(sizeof(struct NCR_700_Host_Parameters), GFP_KERNEL);
	if (!hostdata) {
		printk(KERN_ERR "zorro7xx: Failed to allocate host data\n");
		goto out_release;
	}

	/* Fill in the required pieces of hostdata */
	if (ioaddr > 0x01000000)
		hostdata->base = ioremap(ioaddr, zorro_resource_len(z));
	else
		hostdata->base = (void __iomem *)ZTWO_VADDR(ioaddr);

	hostdata->clock = 50;
	hostdata->chip710 = 1;

	/* Settings for at least WarpEngine 40xx */
	hostdata->ctest7_extra = CTEST7_TT1;

	zorro7xx_scsi_driver_template.name = zdd->name;

	/* and register the chip */
	host = NCR_700_detect(&zorro7xx_scsi_driver_template, hostdata,
			      &z->dev);
	if (!host) {
		printk(KERN_ERR "zorro7xx: No host detected; "
				"board configuration problem?\n");
		goto out_free;
	}

	host->this_id = 7;
	host->base = ioaddr;
	host->irq = IRQ_AMIGA_PORTS;

	if (request_irq(host->irq, NCR_700_intr, IRQF_SHARED, "zorro7xx-scsi",
			host)) {
		printk(KERN_ERR "zorro7xx: request_irq failed\n");
		goto out_put_host;
	}

	zorro_set_drvdata(z, host);
	scsi_scan_host(host);

	return 0;

 out_put_host:
	scsi_host_put(host);
 out_free:
	if (ioaddr > 0x01000000)
		iounmap(hostdata->base);
	kfree(hostdata);
 out_release:
	zorro_release_device(z);

	return -ENODEV;
}

static void zorro7xx_remove_one(struct zorro_dev *z)
{
	struct Scsi_Host *host = zorro_get_drvdata(z);
	struct NCR_700_Host_Parameters *hostdata = shost_priv(host);

	scsi_remove_host(host);

	NCR_700_release(host);
	kfree(hostdata);
	free_irq(host->irq, host);
	zorro_release_device(z);
}

static struct zorro_driver zorro7xx_driver = {
	.name	  = "zorro7xx-scsi",
	.id_table = zorro7xx_zorro_tbl,
	.probe	  = zorro7xx_init_one,
	.remove	  = zorro7xx_remove_one,
};

static int __init zorro7xx_scsi_init(void)
{
	return zorro_register_driver(&zorro7xx_driver);
}

static void __exit zorro7xx_scsi_exit(void)
{
	zorro_unregister_driver(&zorro7xx_driver);
}

module_init(zorro7xx_scsi_init);
module_exit(zorro7xx_scsi_exit);
