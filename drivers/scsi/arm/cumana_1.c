/*
 * Generic Generic NCR5380 driver
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
#include <asm/system.h>

#include "../scsi.h"
#include <scsi/scsi_host.h>

#include <scsi/scsicam.h>

#define AUTOSENSE
#define PSEUDO_DMA

#define CUMANASCSI_PUBLIC_RELEASE 1

#define NCR5380_implementation_fields	int port, ctrl
#define NCR5380_local_declare()		struct Scsi_Host *_instance
#define NCR5380_setup(instance)		_instance = instance
#define NCR5380_read(reg)		cumanascsi_read(_instance, reg)
#define NCR5380_write(reg, value)	cumanascsi_write(_instance, reg, value)
#define NCR5380_intr			cumanascsi_intr
#define NCR5380_queue_command		cumanascsi_queue_command
#define NCR5380_proc_info		cumanascsi_proc_info

#define BOARD_NORMAL	0
#define BOARD_NCR53C400	1

#include "../NCR5380.h"

void cumanascsi_setup(char *str, int *ints)
{
}

const char *cumanascsi_info(struct Scsi_Host *spnt)
{
	return "";
}

#ifdef NOT_EFFICIENT
#define CTRL(p,v)     outb(*ctrl = (v), (p) - 577)
#define STAT(p)       inb((p)+1)
#define IN(p)         inb((p))
#define OUT(v,p)      outb((v), (p))
#else
#define CTRL(p,v)	(p[-2308] = (*ctrl = (v)))
#define STAT(p)		(p[4])
#define IN(p)		(*(p))
#define IN2(p)		((unsigned short)(*(volatile unsigned long *)(p)))
#define OUT(v,p)	(*(p) = (v))
#define OUT2(v,p)	(*((volatile unsigned long *)(p)) = (v))
#endif
#define L(v)		(((v)<<16)|((v) & 0x0000ffff))
#define H(v)		(((v)>>16)|((v) & 0xffff0000))

static inline int
NCR5380_pwrite(struct Scsi_Host *instance, unsigned char *addr, int len)
{
  int *ctrl = &((struct NCR5380_hostdata *)instance->hostdata)->ctrl;
  int oldctrl = *ctrl;
  unsigned long *laddr;
#ifdef NOT_EFFICIENT
  int iobase = instance->io_port;
  int dma_io = iobase & ~(0x3C0000>>2);
#else
  volatile unsigned char *iobase = (unsigned char *)ioaddr(instance->io_port);
  volatile unsigned char *dma_io = (unsigned char *)((int)iobase & ~0x3C0000);
#endif

  if(!len) return 0;

  CTRL(iobase, 0x02);
  laddr = (unsigned long *)addr;
  while(len >= 32)
  {
    int status;
    unsigned long v;
    status = STAT(iobase);
    if(status & 0x80)
      goto end;
    if(!(status & 0x40))
      continue;
    v=*laddr++; OUT2(L(v),dma_io); OUT2(H(v),dma_io);
    v=*laddr++; OUT2(L(v),dma_io); OUT2(H(v),dma_io);
    v=*laddr++; OUT2(L(v),dma_io); OUT2(H(v),dma_io);
    v=*laddr++; OUT2(L(v),dma_io); OUT2(H(v),dma_io);
    v=*laddr++; OUT2(L(v),dma_io); OUT2(H(v),dma_io);
    v=*laddr++; OUT2(L(v),dma_io); OUT2(H(v),dma_io);
    v=*laddr++; OUT2(L(v),dma_io); OUT2(H(v),dma_io);
    v=*laddr++; OUT2(L(v),dma_io); OUT2(H(v),dma_io);
    len -= 32;
    if(len == 0)
      break;
  }

  addr = (unsigned char *)laddr;
  CTRL(iobase, 0x12);
  while(len > 0)
  {
    int status;
    status = STAT(iobase);
    if(status & 0x80)
      goto end;
    if(status & 0x40)
    {
      OUT(*addr++, dma_io);
      if(--len == 0)
        break;
    }

    status = STAT(iobase);
    if(status & 0x80)
      goto end;
    if(status & 0x40)
    {
      OUT(*addr++, dma_io);
      if(--len == 0)
        break;
    }
  }
end:
  CTRL(iobase, oldctrl|0x40);
  return len;
}

static inline int
NCR5380_pread(struct Scsi_Host *instance, unsigned char *addr, int len)
{
  int *ctrl = &((struct NCR5380_hostdata *)instance->hostdata)->ctrl;
  int oldctrl = *ctrl;
  unsigned long *laddr;
#ifdef NOT_EFFICIENT
  int iobase = instance->io_port;
  int dma_io = iobase & ~(0x3C0000>>2);
#else
  volatile unsigned char *iobase = (unsigned char *)ioaddr(instance->io_port);
  volatile unsigned char *dma_io = (unsigned char *)((int)iobase & ~0x3C0000);
#endif

  if(!len) return 0;

  CTRL(iobase, 0x00);
  laddr = (unsigned long *)addr;
  while(len >= 32)
  {
    int status;
    status = STAT(iobase);
    if(status & 0x80)
      goto end;
    if(!(status & 0x40))
      continue;
    *laddr++ = IN2(dma_io)|(IN2(dma_io)<<16);
    *laddr++ = IN2(dma_io)|(IN2(dma_io)<<16);
    *laddr++ = IN2(dma_io)|(IN2(dma_io)<<16);
    *laddr++ = IN2(dma_io)|(IN2(dma_io)<<16);
    *laddr++ = IN2(dma_io)|(IN2(dma_io)<<16);
    *laddr++ = IN2(dma_io)|(IN2(dma_io)<<16);
    *laddr++ = IN2(dma_io)|(IN2(dma_io)<<16);
    *laddr++ = IN2(dma_io)|(IN2(dma_io)<<16);
    len -= 32;
    if(len == 0)
      break;
  }

  addr = (unsigned char *)laddr;
  CTRL(iobase, 0x10);
  while(len > 0)
  {
    int status;
    status = STAT(iobase);
    if(status & 0x80)
      goto end;
    if(status & 0x40)
    {
      *addr++ = IN(dma_io);
      if(--len == 0)
        break;
    }

    status = STAT(iobase);
    if(status & 0x80)
      goto end;
    if(status & 0x40)
    {
      *addr++ = IN(dma_io);
      if(--len == 0)
        break;
    }
  }
end:
  CTRL(iobase, oldctrl|0x40);
  return len;
}

#undef STAT
#undef CTRL
#undef IN
#undef OUT

#define CTRL(p,v) outb(*ctrl = (v), (p) - 577)

static char cumanascsi_read(struct Scsi_Host *instance, int reg)
{
	unsigned int iobase = instance->io_port;
	int i;
	int *ctrl = &((struct NCR5380_hostdata *)instance->hostdata)->ctrl;

	CTRL(iobase, 0);
	i = inb(iobase + 64 + reg);
	CTRL(iobase, 0x40);

	return i;
}

static void cumanascsi_write(struct Scsi_Host *instance, int reg, int value)
{
	int iobase = instance->io_port;
	int *ctrl = &((struct NCR5380_hostdata *)instance->hostdata)->ctrl;

	CTRL(iobase, 0);
	outb(value, iobase + 64 + reg);
	CTRL(iobase, 0x40);
}

#undef CTRL

#include "../NCR5380.c"

static struct scsi_host_template cumanascsi_template = {
	.module			= THIS_MODULE,
	.name			= "Cumana 16-bit SCSI",
	.info			= cumanascsi_info,
	.queuecommand		= cumanascsi_queue_command,
	.eh_abort_handler	= NCR5380_abort,
	.eh_bus_reset_handler	= NCR5380_bus_reset,
	.can_queue		= 16,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 2,
	.unchecked_isa_dma	= 0,
	.use_clustering		= DISABLE_CLUSTERING,
	.proc_name		= "CumanaSCSI-1",
};

static int __devinit
cumanascsi1_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct Scsi_Host *host;
	int ret = -ENOMEM;

	host = scsi_host_alloc(&cumanascsi_template, sizeof(struct NCR5380_hostdata));
	if (!host)
		goto out;

        host->io_port = ecard_address(ec, ECARD_IOC, ECARD_SLOW) + 0x800;
	host->irq = ec->irq;

	NCR5380_init(host, 0);

	host->n_io_port = 255;
	if (!(request_region(host->io_port, host->n_io_port, "CumanaSCSI-1"))) {
		ret = -EBUSY;
		goto out_free;
	}

        ((struct NCR5380_hostdata *)host->hostdata)->ctrl = 0;
        outb(0x00, host->io_port - 577);

	ret = request_irq(host->irq, cumanascsi_intr, IRQF_DISABLED,
			  "CumanaSCSI-1", host);
	if (ret) {
		printk("scsi%d: IRQ%d not free: %d\n",
		    host->host_no, host->irq, ret);
		goto out_release;
	}

	printk("scsi%d: at port 0x%08lx irq %d",
		host->host_no, host->io_port, host->irq);
	printk(" options CAN_QUEUE=%d CMD_PER_LUN=%d release=%d",
		host->can_queue, host->cmd_per_lun, CUMANASCSI_PUBLIC_RELEASE);
	printk("\nscsi%d:", host->host_no);
	NCR5380_print_options(host);
	printk("\n");

	ret = scsi_add_host(host, &ec->dev);
	if (ret)
		goto out_free_irq;

	scsi_scan_host(host);
	goto out;

 out_free_irq:
	free_irq(host->irq, host);
 out_release:
	release_region(host->io_port, host->n_io_port);
 out_free:
	scsi_host_put(host);
 out:
	return ret;
}

static void __devexit cumanascsi1_remove(struct expansion_card *ec)
{
	struct Scsi_Host *host = ecard_get_drvdata(ec);

	ecard_set_drvdata(ec, NULL);

	scsi_remove_host(host);
	free_irq(host->irq, host);
	NCR5380_exit(host);
	release_region(host->io_port, host->n_io_port);
	scsi_host_put(host);
}

static const struct ecard_id cumanascsi1_cids[] = {
	{ MANU_CUMANA, PROD_CUMANA_SCSI_1 },
	{ 0xffff, 0xffff }
};

static struct ecard_driver cumanascsi1_driver = {
	.probe		= cumanascsi1_probe,
	.remove		= __devexit_p(cumanascsi1_remove),
	.id_table	= cumanascsi1_cids,
	.drv = {
		.name		= "cumanascsi1",
	},
};

static int __init cumanascsi_init(void)
{
	return ecard_register_driver(&cumanascsi1_driver);
}

static void __exit cumanascsi_exit(void)
{
	ecard_remove_driver(&cumanascsi1_driver);
}

module_init(cumanascsi_init);
module_exit(cumanascsi_exit);

MODULE_DESCRIPTION("Cumana SCSI-1 driver for Acorn machines");
MODULE_LICENSE("GPL");
