/* cyberstormII.c: Driver for CyberStorm SCSI Mk II
 *
 * Copyright (C) 1996 Jesper Skov (jskov@cygnus.co.uk)
 *
 * This driver is based on cyberstorm.c
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

/* The controller registers can be found in the Z2 config area at these
 * offsets:
 */
#define CYBERII_ESP_ADDR 0x1ff03
#define CYBERII_DMA_ADDR 0x1ff43


/* The CyberStorm II DMA interface */
struct cyberII_dma_registers {
	volatile unsigned char cond_reg;        /* DMA cond    (ro)  [0x000] */
#define ctrl_reg  cond_reg			/* DMA control (wo)  [0x000] */
	unsigned char dmapad4[0x3f];
	volatile unsigned char dma_addr0;	/* DMA address (MSB) [0x040] */
	unsigned char dmapad1[3];
	volatile unsigned char dma_addr1;	/* DMA address       [0x044] */
	unsigned char dmapad2[3];
	volatile unsigned char dma_addr2;	/* DMA address       [0x048] */
	unsigned char dmapad3[3];
	volatile unsigned char dma_addr3;	/* DMA address (LSB) [0x04c] */
};

/* DMA control bits */
#define CYBERII_DMA_LED    0x02	/* HD led control 1 = on */

static int  dma_bytes_sent(struct NCR_ESP *esp, int fifo_count);
static int  dma_can_transfer(struct NCR_ESP *esp, Scsi_Cmnd *sp);
static void dma_dump_state(struct NCR_ESP *esp);
static void dma_init_read(struct NCR_ESP *esp, __u32 addr, int length);
static void dma_init_write(struct NCR_ESP *esp, __u32 addr, int length);
static void dma_ints_off(struct NCR_ESP *esp);
static void dma_ints_on(struct NCR_ESP *esp);
static int  dma_irq_p(struct NCR_ESP *esp);
static void dma_led_off(struct NCR_ESP *esp);
static void dma_led_on(struct NCR_ESP *esp);
static int  dma_ports_p(struct NCR_ESP *esp);
static void dma_setup(struct NCR_ESP *esp, __u32 addr, int count, int write);

static volatile unsigned char cmd_buffer[16];
				/* This is where all commands are put
				 * before they are transferred to the ESP chip
				 * via PIO.
				 */

/***************************************************************** Detection */
int __init cyberII_esp_detect(struct scsi_host_template *tpnt)
{
	struct NCR_ESP *esp;
	struct zorro_dev *z = NULL;
	unsigned long address;
	struct ESP_regs *eregs;

	if ((z = zorro_find_device(ZORRO_PROD_PHASE5_CYBERSTORM_MK_II, z))) {
	    unsigned long board = z->resource.start;
	    if (request_mem_region(board+CYBERII_ESP_ADDR,
				   sizeof(struct ESP_regs), "NCR53C9x")) {
		/* Do some magic to figure out if the CyberStorm Mk II
		 * is equipped with a SCSI controller
		 */
		address = (unsigned long)ZTWO_VADDR(board);
		eregs = (struct ESP_regs *)(address + CYBERII_ESP_ADDR);

		esp = esp_allocate(tpnt, (void *)board+CYBERII_ESP_ADDR);

		esp_write(eregs->esp_cfg1, (ESP_CONFIG1_PENABLE | 7));
		udelay(5);
		if(esp_read(eregs->esp_cfg1) != (ESP_CONFIG1_PENABLE | 7)) {
			esp_deallocate(esp);
			scsi_unregister(esp->ehost);
			release_mem_region(board+CYBERII_ESP_ADDR,
					   sizeof(struct ESP_regs));
			return 0; /* Bail out if address did not hold data */
		}

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
		esp->dma_led_on = &dma_led_on;
		esp->dma_led_off = &dma_led_off;
		esp->dma_poll = 0;
		esp->dma_reset = 0;

		/* SCSI chip speed */
		esp->cfreq = 40000000;

		/* The DMA registers on the CyberStorm are mapped
		 * relative to the device (i.e. in the same Zorro
		 * I/O block).
		 */
		esp->dregs = (void *)(address + CYBERII_DMA_ADDR);

		/* ESP register base */
		esp->eregs = eregs;
		
		/* Set the command buffer */
		esp->esp_command = cmd_buffer;
		esp->esp_command_dvma = virt_to_bus((void *)cmd_buffer);

		esp->irq = IRQ_AMIGA_PORTS;
		request_irq(IRQ_AMIGA_PORTS, esp_intr, SA_SHIRQ,
			    "CyberStorm SCSI Mk II", esp->ehost);

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
}

/************************************************************* DMA Functions */
static int dma_bytes_sent(struct NCR_ESP *esp, int fifo_count)
{
	/* Since the CyberStorm DMA is fully dedicated to the ESP chip,
	 * the number of bytes sent (to the ESP chip) equals the number
	 * of bytes in the FIFO - there is no buffering in the DMA controller.
	 * XXXX Do I read this right? It is from host to ESP, right?
	 */
	return fifo_count;
}

static int dma_can_transfer(struct NCR_ESP *esp, Scsi_Cmnd *sp)
{
	/* I don't think there's any limit on the CyberDMA. So we use what
	 * the ESP chip can handle (24 bit).
	 */
	unsigned long sz = sp->SCp.this_residual;
	if(sz > 0x1000000)
		sz = 0x1000000;
	return sz;
}

static void dma_dump_state(struct NCR_ESP *esp)
{
	ESPLOG(("esp%d: dma -- cond_reg<%02x>\n",
		esp->esp_id, ((struct cyberII_dma_registers *)
			      (esp->dregs))->cond_reg));
	ESPLOG(("intreq:<%04x>, intena:<%04x>\n",
		custom.intreqr, custom.intenar));
}

static void dma_init_read(struct NCR_ESP *esp, __u32 addr, int length)
{
	struct cyberII_dma_registers *dregs = 
		(struct cyberII_dma_registers *) esp->dregs;

	cache_clear(addr, length);

	addr &= ~(1);
	dregs->dma_addr0 = (addr >> 24) & 0xff;
	dregs->dma_addr1 = (addr >> 16) & 0xff;
	dregs->dma_addr2 = (addr >>  8) & 0xff;
	dregs->dma_addr3 = (addr      ) & 0xff;
}

static void dma_init_write(struct NCR_ESP *esp, __u32 addr, int length)
{
	struct cyberII_dma_registers *dregs = 
		(struct cyberII_dma_registers *) esp->dregs;

	cache_push(addr, length);

	addr |= 1;
	dregs->dma_addr0 = (addr >> 24) & 0xff;
	dregs->dma_addr1 = (addr >> 16) & 0xff;
	dregs->dma_addr2 = (addr >>  8) & 0xff;
	dregs->dma_addr3 = (addr      ) & 0xff;
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
	/* It's important to check the DMA IRQ bit in the correct way! */
	return (esp_read(esp->eregs->esp_status) & ESP_STAT_INTR);
}

static void dma_led_off(struct NCR_ESP *esp)
{
	((struct cyberII_dma_registers *)(esp->dregs))->ctrl_reg &= ~CYBERII_DMA_LED;
}

static void dma_led_on(struct NCR_ESP *esp)
{
	((struct cyberII_dma_registers *)(esp->dregs))->ctrl_reg |= CYBERII_DMA_LED;
}

static int dma_ports_p(struct NCR_ESP *esp)
{
	return ((custom.intenar) & IF_PORTS);
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

int cyberII_esp_release(struct Scsi_Host *instance)
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
	.proc_name		= "esp-cyberstormII",
	.proc_info		= esp_proc_info,
	.name			= "CyberStorm Mk II SCSI",
	.detect			= cyberII_esp_detect,
	.slave_alloc		= esp_slave_alloc,
	.slave_destroy		= esp_slave_destroy,
	.release		= cyberII_esp_release,
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
