/* fastlane.c: Driver for Phase5's Fastlane SCSI Controller.
 *
 * Copyright (C) 1996 Jesper Skov (jskov@cygnus.co.uk)
 *
 * This driver is based on the CyberStorm driver, hence the occasional
 * reference to CyberStorm.
 *
 * Betatesting & crucial adjustments by
 *        Patrik Rak (prak3264@ss1000.ms.mff.cuni.cz)
 *
 */

/* TODO:
 *
 * o According to the doc from laire, it is required to reset the DMA when
 *   the transfer is done. ATM we reset DMA just before every new 
 *   dma_init_(read|write).
 *
 * 1) Figure out how to make a cleaner merge with the sparc driver with regard
 *    to the caches and the Sparc MMU mapping.
 * 2) Make as few routines required outside the generic driver. A lot of the
 *    routines in this file used to be inline!
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/interrupt.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "NCR53C9x.h"

#include <linux/zorro.h>
#include <asm/irq.h>

#include <asm/amigaints.h>
#include <asm/amigahw.h>

#include <asm/pgtable.h>

/* Such day has just come... */
#if 0
/* Let this defined unless you really need to enable DMA IRQ one day */
#define NODMAIRQ
#endif

/* The controller registers can be found in the Z2 config area at these
 * offsets:
 */
#define FASTLANE_ESP_ADDR 0x1000001
#define FASTLANE_DMA_ADDR 0x1000041


/* The Fastlane DMA interface */
struct fastlane_dma_registers {
	volatile unsigned char cond_reg;	/* DMA status  (ro) [0x0000] */
#define ctrl_reg  cond_reg			/* DMA control (wo) [0x0000] */
	unsigned char dmapad1[0x3f];
	volatile unsigned char clear_strobe;    /* DMA clear   (wo) [0x0040] */
};


/* DMA status bits */
#define FASTLANE_DMA_MINT  0x80
#define FASTLANE_DMA_IACT  0x40
#define FASTLANE_DMA_CREQ  0x20

/* DMA control bits */
#define FASTLANE_DMA_FCODE 0xa0
#define FASTLANE_DMA_MASK  0xf3
#define FASTLANE_DMA_LED   0x10	/* HD led control 1 = on */
#define FASTLANE_DMA_WRITE 0x08 /* 1 = write */
#define FASTLANE_DMA_ENABLE 0x04 /* Enable DMA */
#define FASTLANE_DMA_EDI   0x02	/* Enable DMA IRQ ? */
#define FASTLANE_DMA_ESI   0x01	/* Enable SCSI IRQ */

static int  dma_bytes_sent(struct NCR_ESP *esp, int fifo_count);
static int  dma_can_transfer(struct NCR_ESP *esp, Scsi_Cmnd *sp);
static void dma_dump_state(struct NCR_ESP *esp);
static void dma_init_read(struct NCR_ESP *esp, __u32 addr, int length);
static void dma_init_write(struct NCR_ESP *esp, __u32 vaddr, int length);
static void dma_ints_off(struct NCR_ESP *esp);
static void dma_ints_on(struct NCR_ESP *esp);
static int  dma_irq_p(struct NCR_ESP *esp);
static void dma_irq_exit(struct NCR_ESP *esp);
static void dma_led_off(struct NCR_ESP *esp);
static void dma_led_on(struct NCR_ESP *esp);
static int  dma_ports_p(struct NCR_ESP *esp);
static void dma_setup(struct NCR_ESP *esp, __u32 addr, int count, int write);

static unsigned char ctrl_data = 0;	/* Keep backup of the stuff written
				 * to ctrl_reg. Always write a copy
				 * to this register when writing to
				 * the hardware register!
				 */

static volatile unsigned char cmd_buffer[16];
				/* This is where all commands are put
				 * before they are transferred to the ESP chip
				 * via PIO.
				 */

static inline void dma_clear(struct NCR_ESP *esp)
{
	struct fastlane_dma_registers *dregs =
		(struct fastlane_dma_registers *) (esp->dregs);
	unsigned long *t;

	ctrl_data = (ctrl_data & FASTLANE_DMA_MASK);
	dregs->ctrl_reg = ctrl_data;

	t = (unsigned long *)(esp->edev);

	dregs->clear_strobe = 0;
	*t = 0 ;
}

/***************************************************************** Detection */
int __init fastlane_esp_detect(struct scsi_host_template *tpnt)
{
	struct NCR_ESP *esp;
	struct zorro_dev *z = NULL;
	unsigned long address;

	if ((z = zorro_find_device(ZORRO_PROD_PHASE5_BLIZZARD_1230_II_FASTLANE_Z3_CYBERSCSI_CYBERSTORM060, z))) {
	    unsigned long board = z->resource.start;
	    if (request_mem_region(board+FASTLANE_ESP_ADDR,
				   sizeof(struct ESP_regs), "NCR53C9x")) {
		/* Check if this is really a fastlane controller. The problem
		 * is that also the cyberstorm and blizzard controllers use
		 * this ID value. Fortunately only Fastlane maps in Z3 space
		 */
		if (board < 0x1000000) {
			goto err_release;
		}
		esp = esp_allocate(tpnt, (void *)board+FASTLANE_ESP_ADDR);

		/* Do command transfer with programmed I/O */
		esp->do_pio_cmds = 1;

		/* Required functions */
		esp->dma_bytes_sent = &dma_bytes_sent;
		esp->dma_can_transfer = &dma_can_transfer;
		esp->dma_dump_state = &dma_dump_state;
		esp->dma_init_read = &dma_init_read;
		esp->dma_init_write = &dma_init_write;
		esp->dma_ints_off = &dma_ints_off;
		esp->dma_ints_on = &dma_ints_on;
		esp->dma_irq_p = &dma_irq_p;
		esp->dma_ports_p = &dma_ports_p;
		esp->dma_setup = &dma_setup;

		/* Optional functions */
		esp->dma_barrier = 0;
		esp->dma_drain = 0;
		esp->dma_invalidate = 0;
		esp->dma_irq_entry = 0;
		esp->dma_irq_exit = &dma_irq_exit;
		esp->dma_led_on = &dma_led_on;
		esp->dma_led_off = &dma_led_off;
		esp->dma_poll = 0;
		esp->dma_reset = 0;

		/* Initialize the portBits (enable IRQs) */
		ctrl_data = (FASTLANE_DMA_FCODE |
#ifndef NODMAIRQ
			     FASTLANE_DMA_EDI |
#endif
			     FASTLANE_DMA_ESI);
			

		/* SCSI chip clock */
		esp->cfreq = 40000000;


		/* Map the physical address space into virtual kernel space */
		address = (unsigned long)
			z_ioremap(board, z->resource.end-board+1);

		if(!address){
			printk("Could not remap Fastlane controller memory!");
			goto err_unregister;
		}


		/* The DMA registers on the Fastlane are mapped
		 * relative to the device (i.e. in the same Zorro
		 * I/O block).
		 */
		esp->dregs = (void *)(address + FASTLANE_DMA_ADDR);

		/* ESP register base */
		esp->eregs = (struct ESP_regs *)(address + FASTLANE_ESP_ADDR);

		/* Board base */
		esp->edev = (void *) address;
		
		/* Set the command buffer */
		esp->esp_command = cmd_buffer;
		esp->esp_command_dvma = virt_to_bus((void *)cmd_buffer);

		esp->irq = IRQ_AMIGA_PORTS;
		esp->slot = board+FASTLANE_ESP_ADDR;
		if (request_irq(IRQ_AMIGA_PORTS, esp_intr, SA_SHIRQ,
				"Fastlane SCSI", esp->ehost)) {
			printk(KERN_WARNING "Fastlane: Could not get IRQ%d, aborting.\n", IRQ_AMIGA_PORTS);
			goto err_unmap;
		}			

		/* Controller ID */
		esp->scsi_id = 7;
		
		/* We don't have a differential SCSI-bus. */
		esp->diff = 0;

		dma_clear(esp);
		esp_initialize(esp);

		printk("ESP: Total of %d ESP hosts found, %d actually in use.\n", nesps, esps_in_use);
		esps_running = esps_in_use;
		return esps_in_use;
	    }
	}
	return 0;

 err_unmap:
	z_iounmap((void *)address);
 err_unregister:
	scsi_unregister (esp->ehost);
 err_release:
	release_mem_region(z->resource.start+FASTLANE_ESP_ADDR,
			   sizeof(struct ESP_regs));
	return 0;
}


/************************************************************* DMA Functions */
static int dma_bytes_sent(struct NCR_ESP *esp, int fifo_count)
{
	/* Since the Fastlane DMA is fully dedicated to the ESP chip,
	 * the number of bytes sent (to the ESP chip) equals the number
	 * of bytes in the FIFO - there is no buffering in the DMA controller.
	 * XXXX Do I read this right? It is from host to ESP, right?
	 */
	return fifo_count;
}

static int dma_can_transfer(struct NCR_ESP *esp, Scsi_Cmnd *sp)
{
	unsigned long sz = sp->SCp.this_residual;
	if(sz > 0xfffc)
		sz = 0xfffc;
	return sz;
}

static void dma_dump_state(struct NCR_ESP *esp)
{
	ESPLOG(("esp%d: dma -- cond_reg<%02x>\n",
		esp->esp_id, ((struct fastlane_dma_registers *)
			      (esp->dregs))->cond_reg));
	ESPLOG(("intreq:<%04x>, intena:<%04x>\n",
		amiga_custom.intreqr, amiga_custom.intenar));
}

static void dma_init_read(struct NCR_ESP *esp, __u32 addr, int length)
{
	struct fastlane_dma_registers *dregs = 
		(struct fastlane_dma_registers *) (esp->dregs);
	unsigned long *t;
	
	cache_clear(addr, length);

	dma_clear(esp);

	t = (unsigned long *)((addr & 0x00ffffff) + esp->edev);

	dregs->clear_strobe = 0;
	*t = addr;

	ctrl_data = (ctrl_data & FASTLANE_DMA_MASK) | FASTLANE_DMA_ENABLE;
	dregs->ctrl_reg = ctrl_data;
}

static void dma_init_write(struct NCR_ESP *esp, __u32 addr, int length)
{
	struct fastlane_dma_registers *dregs = 
		(struct fastlane_dma_registers *) (esp->dregs);
	unsigned long *t;

	cache_push(addr, length);

	dma_clear(esp);

	t = (unsigned long *)((addr & 0x00ffffff) + (esp->edev));

	dregs->clear_strobe = 0;
	*t = addr;

	ctrl_data = ((ctrl_data & FASTLANE_DMA_MASK) | 
		     FASTLANE_DMA_ENABLE |
		     FASTLANE_DMA_WRITE);
	dregs->ctrl_reg = ctrl_data;
}


static void dma_ints_off(struct NCR_ESP *esp)
{
	disable_irq(esp->irq);
}

static void dma_ints_on(struct NCR_ESP *esp)
{
	enable_irq(esp->irq);
}

static void dma_irq_exit(struct NCR_ESP *esp)
{
	struct fastlane_dma_registers *dregs = 
		(struct fastlane_dma_registers *) (esp->dregs);

	dregs->ctrl_reg = ctrl_data & ~(FASTLANE_DMA_EDI|FASTLANE_DMA_ESI);
#ifdef __mc68000__
	nop();
#endif
	dregs->ctrl_reg = ctrl_data;
}

static int dma_irq_p(struct NCR_ESP *esp)
{
	struct fastlane_dma_registers *dregs = 
		(struct fastlane_dma_registers *) (esp->dregs);
	unsigned char dma_status;

	dma_status = dregs->cond_reg;

	if(dma_status & FASTLANE_DMA_IACT)
		return 0;	/* not our IRQ */

	/* Return non-zero if ESP requested IRQ */
	return (
#ifndef NODMAIRQ
	   (dma_status & FASTLANE_DMA_CREQ) &&
#endif
	   (!(dma_status & FASTLANE_DMA_MINT)) &&
	   (esp_read(((struct ESP_regs *) (esp->eregs))->esp_status) & ESP_STAT_INTR));
}

static void dma_led_off(struct NCR_ESP *esp)
{
	ctrl_data &= ~FASTLANE_DMA_LED;
	((struct fastlane_dma_registers *)(esp->dregs))->ctrl_reg = ctrl_data;
}

static void dma_led_on(struct NCR_ESP *esp)
{
	ctrl_data |= FASTLANE_DMA_LED;
	((struct fastlane_dma_registers *)(esp->dregs))->ctrl_reg = ctrl_data;
}

static int dma_ports_p(struct NCR_ESP *esp)
{
	return ((amiga_custom.intenar) & IF_PORTS);
}

static void dma_setup(struct NCR_ESP *esp, __u32 addr, int count, int write)
{
	/* On the Sparc, DMA_ST_WRITE means "move data from device to memory"
	 * so when (write) is true, it actually means READ!
	 */
	if(write){
		dma_init_read(esp, addr, count);
	} else {
		dma_init_write(esp, addr, count);
	}
}

#define HOSTS_C

int fastlane_esp_release(struct Scsi_Host *instance)
{
#ifdef MODULE
	unsigned long address = (unsigned long)((struct NCR_ESP *)instance->hostdata)->edev;
	esp_deallocate((struct NCR_ESP *)instance->hostdata);
	esp_release();
	release_mem_region(address, sizeof(struct ESP_regs));
	free_irq(IRQ_AMIGA_PORTS, esp_intr);
#endif
	return 1;
}


static struct scsi_host_template driver_template = {
	.proc_name		= "esp-fastlane",
	.proc_info		= esp_proc_info,
	.name			= "Fastlane SCSI",
	.detect			= fastlane_esp_detect,
	.slave_alloc		= esp_slave_alloc,
	.slave_destroy		= esp_slave_destroy,
	.release		= fastlane_esp_release,
	.queuecommand		= esp_queue,
	.eh_abort_handler	= esp_abort,
	.eh_bus_reset_handler	= esp_reset,
	.can_queue		= 7,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 1,
	.use_clustering		= ENABLE_CLUSTERING
};

#include "scsi_module.c"

MODULE_LICENSE("GPL");
