/* cyberstorm.c: Driver for CyberStorm SCSI Controller.
 *
 * Copyright (C) 1996 Jesper Skov (jskov@cygnus.co.uk)
 *
 * The CyberStorm SCSI driver is based on David S. Miller's ESP driver
 * for the Sparc computers. 
 * 
 * This work was made possible by Phase5 who willingly (and most generously)
 * supported me with hardware and all the information I needed.
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
#define CYBER_ESP_ADDR 0xf400
#define CYBER_DMA_ADDR 0xf800


/* The CyberStorm DMA interface */
struct cyber_dma_registers {
	volatile unsigned char dma_addr0;	/* DMA address (MSB) [0x000] */
	unsigned char dmapad1[1];
	volatile unsigned char dma_addr1;	/* DMA address       [0x002] */
	unsigned char dmapad2[1];
	volatile unsigned char dma_addr2;	/* DMA address       [0x004] */
	unsigned char dmapad3[1];
	volatile unsigned char dma_addr3;	/* DMA address (LSB) [0x006] */
	unsigned char dmapad4[0x3fb];
	volatile unsigned char cond_reg;        /* DMA cond    (ro)  [0x402] */
#define ctrl_reg  cond_reg			/* DMA control (wo)  [0x402] */
};

/* DMA control bits */
#define CYBER_DMA_LED    0x80	/* HD led control 1 = on */
#define CYBER_DMA_WRITE  0x40	/* DMA direction. 1 = write */
#define CYBER_DMA_Z3     0x20	/* 16 (Z2) or 32 (CHIP/Z3) bit DMA transfer */

/* DMA status bits */
#define CYBER_DMA_HNDL_INTR 0x80	/* DMA IRQ pending? */

/* The bits below appears to be Phase5 Debug bits only; they were not
 * described by Phase5 so using them may seem a bit stupid...
 */
#define CYBER_HOST_ID 0x02	/* If set, host ID should be 7, otherwise
				 * it should be 6.
				 */
#define CYBER_SLOW_CABLE 0x08	/* If *not* set, assume SLOW_CABLE */

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

/***************************************************************** Detection */
int __init cyber_esp_detect(struct scsi_host_template *tpnt)
{
	struct NCR_ESP *esp;
	struct zorro_dev *z = NULL;
	unsigned long address;

	while ((z = zorro_find_device(ZORRO_WILDCARD, z))) {
	    unsigned long board = z->resource.start;
	    if ((z->id == ZORRO_PROD_PHASE5_BLIZZARD_1220_CYBERSTORM ||
		 z->id == ZORRO_PROD_PHASE5_BLIZZARD_1230_II_FASTLANE_Z3_CYBERSCSI_CYBERSTORM060) &&
		request_mem_region(board+CYBER_ESP_ADDR,
		    		   sizeof(struct ESP_regs), "NCR53C9x")) {
		/* Figure out if this is a CyberStorm or really a 
		 * Fastlane/Blizzard Mk II by looking at the board size.
		 * CyberStorm maps 64kB
		 * (ZORRO_PROD_PHASE5_BLIZZARD_1220_CYBERSTORM does anyway)
		 */
		if(z->resource.end-board != 0xffff) {
			release_mem_region(board+CYBER_ESP_ADDR,
					   sizeof(struct ESP_regs));
			return 0;
		}
		esp = esp_allocate(tpnt, (void *)board+CYBER_ESP_ADDR);

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
		address = (unsigned long)ZTWO_VADDR(board);
		esp->dregs = (void *)(address + CYBER_DMA_ADDR);

		/* ESP register base */
		esp->eregs = (struct ESP_regs *)(address + CYBER_ESP_ADDR);
		
		/* Set the command buffer */
		esp->esp_command = cmd_buffer;
		esp->esp_command_dvma = virt_to_bus((void *)cmd_buffer);

		esp->irq = IRQ_AMIGA_PORTS;
		request_irq(IRQ_AMIGA_PORTS, esp_intr, SA_SHIRQ,
			    "CyberStorm SCSI", esp->ehost);
		/* Figure out our scsi ID on the bus */
		/* The DMA cond flag contains a hardcoded jumper bit
		 * which can be used to select host number 6 or 7.
		 * However, even though it may change, we use a hardcoded
		 * value of 7.
		 */
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
		esp->esp_id, ((struct cyber_dma_registers *)
			      (esp->dregs))->cond_reg));
	ESPLOG(("intreq:<%04x>, intena:<%04x>\n",
		amiga_custom.intreqr, amiga_custom.intenar));
}

static void dma_init_read(struct NCR_ESP *esp, __u32 addr, int length)
{
	struct cyber_dma_registers *dregs = 
		(struct cyber_dma_registers *) esp->dregs;

	cache_clear(addr, length);

	addr &= ~(1);
	dregs->dma_addr0 = (addr >> 24) & 0xff;
	dregs->dma_addr1 = (addr >> 16) & 0xff;
	dregs->dma_addr2 = (addr >>  8) & 0xff;
	dregs->dma_addr3 = (addr      ) & 0xff;
	ctrl_data &= ~(CYBER_DMA_WRITE);

	/* Check if physical address is outside Z2 space and of
	 * block length/block aligned in memory. If this is the
	 * case, enable 32 bit transfer. In all other cases, fall back
	 * to 16 bit transfer.
	 * Obviously 32 bit transfer should be enabled if the DMA address
	 * and length are 32 bit aligned. However, this leads to some
	 * strange behavior. Even 64 bit aligned addr/length fails.
	 * Until I've found a reason for this, 32 bit transfer is only
	 * used for full-block transfers (1kB).
	 *							-jskov
	 */
#if 0
	if((addr & 0x3fc) || length & 0x3ff || ((addr > 0x200000) &&
						(addr < 0xff0000)))
		ctrl_data &= ~(CYBER_DMA_Z3);	/* Z2, do 16 bit DMA */
	else
		ctrl_data |= CYBER_DMA_Z3; /* CHIP/Z3, do 32 bit DMA */
#else
	ctrl_data &= ~(CYBER_DMA_Z3);	/* Z2, do 16 bit DMA */
#endif
	dregs->ctrl_reg = ctrl_data;
}

static void dma_init_write(struct NCR_ESP *esp, __u32 addr, int length)
{
	struct cyber_dma_registers *dregs = 
		(struct cyber_dma_registers *) esp->dregs;

	cache_push(addr, length);

	addr |= 1;
	dregs->dma_addr0 = (addr >> 24) & 0xff;
	dregs->dma_addr1 = (addr >> 16) & 0xff;
	dregs->dma_addr2 = (addr >>  8) & 0xff;
	dregs->dma_addr3 = (addr      ) & 0xff;
	ctrl_data |= CYBER_DMA_WRITE;

	/* See comment above */
#if 0
	if((addr & 0x3fc) || length & 0x3ff || ((addr > 0x200000) &&
						(addr < 0xff0000)))
		ctrl_data &= ~(CYBER_DMA_Z3);	/* Z2, do 16 bit DMA */
	else
		ctrl_data |= CYBER_DMA_Z3; /* CHIP/Z3, do 32 bit DMA */
#else
	ctrl_data &= ~(CYBER_DMA_Z3);	/* Z2, do 16 bit DMA */
#endif
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

static int dma_irq_p(struct NCR_ESP *esp)
{
	/* It's important to check the DMA IRQ bit in the correct way! */
	return ((esp_read(esp->eregs->esp_status) & ESP_STAT_INTR) &&
		((((struct cyber_dma_registers *)(esp->dregs))->cond_reg) &
		 CYBER_DMA_HNDL_INTR));
}

static void dma_led_off(struct NCR_ESP *esp)
{
	ctrl_data &= ~CYBER_DMA_LED;
	((struct cyber_dma_registers *)(esp->dregs))->ctrl_reg = ctrl_data;
}

static void dma_led_on(struct NCR_ESP *esp)
{
	ctrl_data |= CYBER_DMA_LED;
	((struct cyber_dma_registers *)(esp->dregs))->ctrl_reg = ctrl_data;
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

int cyber_esp_release(struct Scsi_Host *instance)
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
	.proc_name		= "esp-cyberstorm",
	.proc_info		= esp_proc_info,
	.name			= "CyberStorm SCSI",
	.detect			= cyber_esp_detect,
	.slave_alloc		= esp_slave_alloc,
	.slave_destroy		= esp_slave_destroy,
	.release		= cyber_esp_release,
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
