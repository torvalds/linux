/*
 * Oak Generic NCR5380 driver
 *
 * Copyright 1995-2002, Russell King
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/init.h>

#include <asm/ecard.h>
#include <asm/io.h>

#include "../scsi.h"
#include <scsi/scsi_host.h>

#define AUTOSENSE
/*#define PSEUDO_DMA*/

#define OAKSCSI_PUBLIC_RELEASE 1

#define priv(host)			((struct NCR5380_hostdata *)(host)->hostdata)
#define NCR5380_local_declare()		void __iomem *_base
#define NCR5380_setup(host)		_base = priv(host)->base

#define NCR5380_read(reg)		readb(_base + ((reg) << 2))
#define NCR5380_write(reg, value)	writeb(value, _base + ((reg) << 2))
#define NCR5380_intr			oakscsi_intr
#define NCR5380_queue_command		oakscsi_queue_command
#define NCR5380_proc_info		oakscsi_proc_info

#define NCR5380_implementation_fields	\
	void __iomem *base

#define BOARD_NORMAL	0
#define BOARD_NCR53C400	1

#include "../NCR5380.h"

#undef START_DMA_INITIATOR_RECEIVE_REG
#define START_DMA_INITIATOR_RECEIVE_REG	(128 + 7)

const char * oakscsi_info (struct Scsi_Host *spnt)
{
	return "";
}

#define STAT	((128 + 16) << 2)
#define DATA	((128 + 8) << 2)

static inline int NCR5380_pwrite(struct Scsi_Host *instance, unsigned char *addr,
              int len)
{
  void __iomem *base = priv(instance)->base;

printk("writing %p len %d\n",addr, len);
  if(!len) return -1;

  while(1)
  {
    int status;
    while (((status = readw(base + STAT)) & 0x100)==0);
  }
}

static inline int NCR5380_pread(struct Scsi_Host *instance, unsigned char *addr,
              int len)
{
  void __iomem *base = priv(instance)->base;
printk("reading %p len %d\n", addr, len);
  while(len > 0)
  {
    unsigned int status, timeout;
    unsigned long b;
    
    timeout = 0x01FFFFFF;
    
    while (((status = readw(base + STAT)) & 0x100)==0)
    {
      timeout--;
      if(status & 0x200 || !timeout)
      {
        printk("status = %08X\n", status);
        return 1;
      }
    }

    if(len >= 128)
    {
      readsw(base + DATA, addr, 128);
      addr += 128;
      len -= 128;
    }
    else
    {
      b = (unsigned long) readw(base + DATA);
      *addr ++ = b;
      len -= 1;
      if(len)
        *addr ++ = b>>8;
      len -= 1;
    }
  }
  return 0;
}

#undef STAT
#undef DATA

#include "../NCR5380.c"

static struct scsi_host_template oakscsi_template = {
	.module			= THIS_MODULE,
	.proc_info		= oakscsi_proc_info,
	.name			= "Oak 16-bit SCSI",
	.info			= oakscsi_info,
	.queuecommand		= oakscsi_queue_command,
	.eh_abort_handler	= NCR5380_abort,
	.eh_bus_reset_handler	= NCR5380_bus_reset,
	.can_queue		= 16,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 2,
	.use_clustering		= DISABLE_CLUSTERING,
	.proc_name		= "oakscsi",
};

static int __devinit
oakscsi_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct Scsi_Host *host;
	int ret = -ENOMEM;

	ret = ecard_request_resources(ec);
	if (ret)
		goto out;

	host = scsi_host_alloc(&oakscsi_template, sizeof(struct NCR5380_hostdata));
	if (!host) {
		ret = -ENOMEM;
		goto release;
	}

	priv(host)->base = ioremap(ecard_resource_start(ec, ECARD_RES_MEMC),
				   ecard_resource_len(ec, ECARD_RES_MEMC));
	if (!priv(host)->base) {
		ret = -ENOMEM;
		goto unreg;
	}

	host->irq = IRQ_NONE;
	host->n_io_port = 255;

	NCR5380_init(host, 0);

	printk("scsi%d: at port 0x%08lx irqs disabled",
		host->host_no, host->io_port);
	printk(" options CAN_QUEUE=%d  CMD_PER_LUN=%d release=%d",
		host->can_queue, host->cmd_per_lun, OAKSCSI_PUBLIC_RELEASE);
	printk("\nscsi%d:", host->host_no);
	NCR5380_print_options(host);
	printk("\n");

	ret = scsi_add_host(host, &ec->dev);
	if (ret)
		goto out_unmap;

	scsi_scan_host(host);
	goto out;

 out_unmap:
	iounmap(priv(host)->base);
 unreg:
	scsi_host_put(host);
 release:
	ecard_release_resources(ec);
 out:
	return ret;
}

static void __devexit oakscsi_remove(struct expansion_card *ec)
{
	struct Scsi_Host *host = ecard_get_drvdata(ec);

	ecard_set_drvdata(ec, NULL);
	scsi_remove_host(host);

	NCR5380_exit(host);
	iounmap(priv(host)->base);
	scsi_host_put(host);
	ecard_release_resources(ec);
}

static const struct ecard_id oakscsi_cids[] = {
	{ MANU_OAK, PROD_OAK_SCSI },
	{ 0xffff, 0xffff }
};

static struct ecard_driver oakscsi_driver = {
	.probe		= oakscsi_probe,
	.remove		= __devexit_p(oakscsi_remove),
	.id_table	= oakscsi_cids,
	.drv = {
		.name		= "oakscsi",
	},
};

static int __init oakscsi_init(void)
{
	return ecard_register_driver(&oakscsi_driver);
}

static void __exit oakscsi_exit(void)
{
	ecard_remove_driver(&oakscsi_driver);
}

module_init(oakscsi_init);
module_exit(oakscsi_exit);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Oak SCSI driver");
MODULE_LICENSE("GPL");

