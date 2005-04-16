#define AUTOSENSE
/* #define PSEUDO_DMA */

/*
 * EcoSCSI Generic NCR5380 driver
 *
 * Copyright 1995, Russell King
 *
 * ALPHA RELEASE 1.
 *
 * For more information, please consult
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/blkdev.h>

#include <asm/io.h>
#include <asm/system.h>

#include "../scsi.h"
#include <scsi/scsi_host.h>

#define NCR5380_implementation_fields	int port, ctrl
#define NCR5380_local_declare()		struct Scsi_Host *_instance
#define NCR5380_setup(instance)		_instance = instance

#define NCR5380_read(reg)		ecoscsi_read(_instance, reg)
#define NCR5380_write(reg, value)	ecoscsi_write(_instance, reg, value)

#define NCR5380_intr			ecoscsi_intr
#define NCR5380_queue_command		ecoscsi_queue_command
#define NCR5380_proc_info		ecoscsi_proc_info

#include "../NCR5380.h"

#define ECOSCSI_PUBLIC_RELEASE 1

static char ecoscsi_read(struct Scsi_Host *instance, int reg)
{
  int iobase = instance->io_port;
  outb(reg | 8, iobase);
  return inb(iobase + 1);
}

static void ecoscsi_write(struct Scsi_Host *instance, int reg, int value)
{
  int iobase = instance->io_port;
  outb(reg | 8, iobase);
  outb(value, iobase + 1);
}

/*
 * Function : ecoscsi_setup(char *str, int *ints)
 *
 * Purpose : LILO command line initialization of the overrides array,
 *
 * Inputs : str - unused, ints - array of integer parameters with ints[0]
 *	equal to the number of ints.
 *
 */

void ecoscsi_setup(char *str, int *ints)
{
}

const char * ecoscsi_info (struct Scsi_Host *spnt)
{
	return "";
}

#if 0
#define STAT(p) inw(p + 144)

static inline int NCR5380_pwrite(struct Scsi_Host *host, unsigned char *addr,
              int len)
{
  int iobase = host->io_port;
printk("writing %p len %d\n",addr, len);
  if(!len) return -1;

  while(1)
  {
    int status;
    while(((status = STAT(iobase)) & 0x100)==0);
  }
}

static inline int NCR5380_pread(struct Scsi_Host *host, unsigned char *addr,
              int len)
{
  int iobase = host->io_port;
  int iobase2= host->io_port + 0x100;
  unsigned char *start = addr;
  int s;
printk("reading %p len %d\n",addr, len);
  outb(inb(iobase + 128), iobase + 135);
  while(len > 0)
  {
    int status,b,i, timeout;
    timeout = 0x07FFFFFF;
    while(((status = STAT(iobase)) & 0x100)==0)
    {
      timeout--;
      if(status & 0x200 || !timeout)
      {
        printk("status = %p\n",status);
        outb(0, iobase + 135);
        return 1;
      }
    }
    if(len >= 128)
    {
      for(i=0; i<64; i++)
      {
        b = inw(iobase + 136);
        *addr++ = b;
        *addr++ = b>>8;
      }
      len -= 128;
    }
    else
    {
      b = inw(iobase + 136);
      *addr ++ = b;
      len -= 1;
      if(len)
        *addr ++ = b>>8;
      len -= 1;
    }
  }
  outb(0, iobase + 135);
  printk("first bytes = %02X %02X %02X %20X %02X %02X %02X\n",*start, start[1], start[2], start[3], start[4], start[5], start[6]);
  return 1;
}
#endif
#undef STAT

#define BOARD_NORMAL	0
#define BOARD_NCR53C400	1

#include "../NCR5380.c"

static Scsi_Host_Template ecoscsi_template =  {
	.module		= THIS_MODULE,
	.name		= "Serial Port EcoSCSI NCR5380",
	.proc_name	= "ecoscsi",
	.info		= ecoscsi_info,
	.queuecommand	= ecoscsi_queue_command,
	.eh_abort_handler	= NCR5380_abort,
	.eh_device_reset_handler= NCR5380_device_reset,
	.eh_bus_reset_handler	= NCR5380_bus_reset,
	.eh_host_reset_handler	= NCR5380_host_reset,
	.can_queue	= 16,
	.this_id	= 7,
	.sg_tablesize	= SG_ALL,
	.cmd_per_lun	= 2,
	.use_clustering	= DISABLE_CLUSTERING
};

static struct Scsi_Host *host;

static int __init ecoscsi_init(void)
{

	host = scsi_host_alloc(tpnt, sizeof(struct NCR5380_hostdata));
	if (!host)
		return 0;

	host->io_port = 0x80ce8000;
	host->n_io_port = 144;
	host->irq = IRQ_NONE;

	if (!(request_region(host->io_port, host->n_io_port, "ecoscsi")) )
		goto unregister_scsi;

	ecoscsi_write(host, MODE_REG, 0x20);		/* Is it really SCSI? */
	if (ecoscsi_read(host, MODE_REG) != 0x20) /* Write to a reg.    */
		goto release_reg;

	ecoscsi_write(host, MODE_REG, 0x00 );		/* it back.	      */
	if (ecoscsi_read(host, MODE_REG) != 0x00)
		goto release_reg;

	NCR5380_init(host, 0);

	printk("scsi%d: at port 0x%08lx irqs disabled", host->host_no, host->io_port);
	printk(" options CAN_QUEUE=%d CMD_PER_LUN=%d release=%d",
		host->can_queue, host->cmd_per_lun, ECOSCSI_PUBLIC_RELEASE);
	printk("\nscsi%d:", host->host_no);
	NCR5380_print_options(host);
	printk("\n");

	scsi_add_host(host, NULL); /* XXX handle failure */
	scsi_scan_host(host);
	return 0;

release_reg:
	release_region(host->io_port, host->n_io_port);
unregister_scsi:
	scsi_host_put(host);
	return -ENODEV;
}

static void __exit ecoscsi_exit(void)
{
	scsi_remove_host(host);

	if (shpnt->irq != IRQ_NONE)
		free_irq(shpnt->irq, NULL);
	NCR5380_exit(host);
	if (shpnt->io_port)
		release_region(shpnt->io_port, shpnt->n_io_port);

	scsi_host_put(host);
	return 0;
}

module_init(ecoscsi_init);
module_exit(ecoscsi_exit);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Econet-SCSI driver for Acorn machines");
MODULE_LICENSE("GPL");

