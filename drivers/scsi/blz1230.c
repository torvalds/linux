/* blz1230.c: Driver for Blizzard 1230 SCSI IV Controller.
 *
 * Copyright (C) 1996 Jesper Skov (jskov@cygnus.co.uk)
 *
 * This driver is based on the CyberStorm driver, hence the occasional
 * reference to CyberStorm.
 */

/* TODO:
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

#define MKIV 1

/* The controller registers can be found in the Z2 config area at these
 * offsets:
 */
#define BLZ1230_ESP_ADDR 0x8000
#define BLZ1230_DMA_ADDR 0x10000
#define BLZ1230II_ESP_ADDR 0x10000
#define BLZ1230II_DMA_ADDR 0x10021


/* The Blizzard 1230 DMA interface
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Only two things can be programmed in the Blizzard DMA:
 *  1) The data direction is controlled by the status of bit 31 (1 = write)
 *  2) The source/dest address (word aligned, shifted one right) in bits 30-0
 *
 * Program DMA by first latching the highest byte of the address/direction
 * (i.e. bits 31-24 of the long word constructed as described in steps 1+2
 * above). Then write each byte of the address/direction (starting with the
 * top byte, working down) to the DMA address register.
 *
 * Figure out interrupt status by reading the ESP status byte.
 */
struct blz1230_dma_registers {
	volatile unsigned char dma_addr; 	/* DMA address      [0x0000] */
	unsigned char dmapad2[0x7fff];
	volatile unsigned char dma_latch; 	/* DMA latch        [0x8000] */
};

struct blz1230II_dma_registers {
	volatile unsigned char dma_addr; 	/* DMA address      [0x0000] */
	unsigned char dmapad2[0xf];
	volatile unsigned char dma_latch; 	/* DMA latch        [0x0010] */
};

#define BLZ1230_DMA_WRITE 0x80000000

static int  dma_bytes_sent(struct NCR_ESP *esp, int fifo_count);
static int  dma_can_transfer(struct NCR_ESP *esp, Scsi_Cmnd *sp);
static void dma_dump_state(struct NCR_ESP *esp);
static void dma_init_read(struct NCR_ESP *esp, __u32 addr, int length);
static void dma_init_write(struct NCR_ESP *esp, __u32 addr, int length);
static void dma_ints_off(struct NCR_ESP *esp);
static void dma_ints_on(struct NCR_ESP *esp);
static int  dma_irq_p(struct NCR_ESP *esp);
static int  dma_ports_p(struct NCR_ESP *esp);
static void dma_setup(struct NCR_ESP *esp, __u32 addr, int count, int write);

static volatile unsigned char cmd_buffer[16];
				/* This is where all commands are put
				 * before they are transferred to the ESP chip
				 * via PIO.
				 */

/***************************************************************** Detection */
int __init blz1230_esp_detect(struct scsi_host_template *tpnt)
{
	struct NCR_ESP *esp;
	struct zorro_dev *z = NULL;
	unsigned long address;
	struct ESP_regs *eregs;
	unsigned long board;

#if MKIV
#define REAL_BLZ1230_ID		ZORRO_PROD_PHASE5_BLIZZARD_1230_IV_1260
#define REAL_BLZ1230_ESP_ADDR	BLZ1230_ESP_ADDR
#define REAL_BLZ1230_DMA_ADDR	BLZ1230_DMA_ADDR
#else
#define REAL_BLZ1230_ID		ZORRO_PROD_PHASE5_BLIZZARD_1230_II_FASTLANE_Z3_CYBERSCSI_CYBERSTORM060
#define REAL_BLZ1230_ESP_ADDR	BLZ1230II_ESP_ADDR
#define REAL_BLZ1230_DMA_ADDR	BLZ1230II_DMA_ADDR
#endif

	if ((z = zorro_find_device(REAL_BLZ1230_ID, z))) {
	    board = z->resource.start;
	    if (request_mem_region(board+REAL_BLZ1230_ESP_ADDR,
				   sizeof(struct ESP_regs), "NCR53C9x")) {
		/* Do some magic to figure out if the blizzard is
		 * equipped with a SCSI controller
		 */
		address = ZTWO_VADDR(board);
		eregs = (struct ESP_regs *)(address + REAL_BLZ1230_ESP_ADDR);
		esp = esp_allocate(tpnt, (void *)board + REAL_BLZ1230_ESP_ADDR,
				   0);

		esp_write(eregs->esp_cfg1, (ESP_CONFIG1_PENABLE | 7));
		udelay(5);
		if(esp_read(eregs->esp_cfg1) != (ESP_CONFIG1_PENABLE | 7))
			goto err_out;

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
		esp->dma_irq_exit = 0;
		esp->dma_led_on = 0;
		esp->dma_led_off = 0;
		esp->dma_poll = 0;
		esp->dma_reset = 0;

		/* SCSI chip speed */
		esp->cfreq = 40000000;

		/* The DMA registers on the Blizzard are mapped
		 * relative to the device (i.e. in the same Zorro
		 * I/O block).
		 */
		esp->dregs = (void *)(address + REAL_BLZ1230_DMA_ADDR);
	
		/* ESP register base */
		esp->eregs = eregs;

		/* Set the command buffer */
		esp->esp_command = cmd_buffer;
		esp->esp_command_dvma = virt_to_bus((void *)cmd_buffer);

		esp->irq = IRQ_AMIGA_PORTS;
		esp->slot = board+REAL_BLZ1230_ESP_ADDR;
		if (request_irq(IRQ_AMIGA_PORTS, esp_intr, IRQF_SHARED,
				 "Blizzard 1230 SCSI IV", esp->ehost))
			goto err_out;

		/* Figure out our scsi ID on the bus */
		esp->scsi_id = 7;
		
		/* We don't have a differential SCSI-bus. */
		esp->diff = 0;

		esp_initialize(esp);

		printk("ESP: Total of %d ESP hosts found, %d actually in use.\n", nesps, esps_in_use);
		esps_running = esps_in_use;
		return esps_in_use;
	    }
	}
	return 0;
 
 err_out:
	scsi_unregister(esp->ehost);
	esp_deallocate(esp);
	release_mem_region(board+REAL_BLZ1230_ESP_ADDR,
			   sizeof(struct ESP_regs));
	return 0;
}

/************************************************************* DMA Functions */
static int dma_bytes_sent(struct NCR_ESP *esp, int fifo_count)
{
	/* Since the Blizzard DMA is fully dedicated to the ESP chip,
	 * the number of bytes sent (to the ESP chip) equals the number
	 * of bytes in the FIFO - there is no buffering in the DMA controller.
	 * XXXX Do I read this right? It is from host to ESP, right?
	 */
	return fifo_count;
}

static int dma_can_transfer(struct NCR_ESP *esp, Scsi_Cmnd *sp)
{
	/* I don't think there's any limit on the Blizzard DMA. So we use what
	 * the ESP chip can handle (24 bit).
	 */
	unsigned long sz = sp->SCp.this_residual;
	if(sz > 0x1000000)
		sz = 0x1000000;
	return sz;
}

static void dma_dump_state(struct NCR_ESP *esp)
{
	ESPLOG(("intreq:<%04x>, intena:<%04x>\n",
		amiga_custom.intreqr, amiga_custom.intenar));
}

void dma_init_read(struct NCR_ESP *esp, __u32 addr, int length)
{
#if MKIV
	struct blz1230_dma_registers *dregs = 
		(struct blz1230_dma_registers *) (esp->dregs);
#else
	struct blz1230II_dma_registers *dregs = 
		(struct blz1230II_dma_registers *) (esp->dregs);
#endif

	cache_clear(addr, length);

	addr >>= 1;
	addr &= ~(BLZ1230_DMA_WRITE);

	/* First set latch */
	dregs->dma_latch = (addr >> 24) & 0xff;

	/* Then pump the address to the DMA address register */
#if MKIV
	dregs->dma_addr = (addr >> 24) & 0xff;
#endif
	dregs->dma_addr = (addr >> 16) & 0xff;
	dregs->dma_addr = (addr >>  8) & 0xff;
	dregs->dma_addr = (addr      ) & 0xff;
}

void dma_init_write(struct NCR_ESP *esp, __u32 addr, int length)
{
#if MKIV
	struct blz1230_dma_registers *dregs = 
		(struct blz1230_dma_registers *) (esp->dregs);
#else
	struct blz1230II_dma_registers *dregs = 
		(struct blz1230II_dma_registers *) (esp->dregs);
#endif

	cache_push(addr, length);

	addr >>= 1;
	addr |= BLZ1230_DMA_WRITE;

	/* First set latch */
	dregs->dma_latch = (addr >> 24) & 0xff;

	/* Then pump the address to the DMA address register */
#if MKIV
	dregs->dma_addr = (addr >> 24) & 0xff;
#endif
	dregs->dma_addr = (addr >> 16) & 0xff;
	dregs->dma_addr = (addr >>  8) & 0xff;
	dregs->dma_addr = (addr      ) & 0xff;
}

static void dma_ints_off(struct NCR_ESP *esp)
{
	disable_irq(esp->irq);
}

static void dma_ints_on(struct NCR_ESP *esp)
{
	enable_irq(esp->irq);
}

static int dma_irq_p(struct NCR_ESP *esp)
{
	return (esp_read(esp->eregs->esp_status) & ESP_STAT_INTR);
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

int blz1230_esp_release(struct Scsi_Host *instance)
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
	.proc_name		= "esp-blz1230",
	.proc_info		= esp_proc_info,
	.name			= "Blizzard1230 SCSI IV",
	.detect			= blz1230_esp_detect,
	.slave_alloc		= esp_slave_alloc,
	.slave_destroy		= esp_slave_destroy,
	.release		= blz1230_esp_release,
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
