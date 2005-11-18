/* mca_53c9x.c: Driver for the SCSI adapter found on NCR 35xx
 *  (and maybe some other) Microchannel machines
 *
 * Code taken mostly from Cyberstorm SCSI drivers
 *   Copyright (C) 1996 Jesper Skov (jskov@cygnus.co.uk)
 *
 * Hacked to work with the NCR MCA stuff by Tymm Twillman (tymm@computer.org)
 *
 * The CyberStorm SCSI driver (and this driver) is based on David S. Miller's
 *   ESP driver  * for the Sparc computers. 
 * 
 * Special thanks to Ken Stewart at Symbios (LSI) for helping with info on
 *  the 86C01.  I was on the brink of going ga-ga...
 *
 * Also thanks to Jesper Skov for helping me with info on how the Amiga
 *  does things...
 */

/*
 * This is currently only set up to use one 53c9x card at a time; it could be 
 *  changed fairly easily to detect/use more than one, but I'm not too sure how
 *  many cards that use the 53c9x on MCA systems there are (if, in fact, there
 *  are cards that use them, other than the one built into some NCR systems)...
 *  If anyone requests this, I'll throw it in, otherwise it's not worth the
 *  effort.
 */

/*
 * Info on the 86C01 MCA interface chip at the bottom, if you care enough to
 *  look.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mca.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/mca-legacy.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "NCR53C9x.h"

#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mca_dma.h>
#include <asm/pgtable.h>

/*
 * From ibmmca.c (IBM scsi controller card driver) -- used for turning PS2 disk
 *  activity LED on and off
 */

#define PS2_SYS_CTR	0x92

/* Ports the ncr's 53c94 can be put at; indexed by pos register value */

#define MCA_53C9X_IO_PORTS {                             \
                         0x0000, 0x0240, 0x0340, 0x0400, \
	                 0x0420, 0x3240, 0x8240, 0xA240, \
	                }
			
/*
 * Supposedly there were some cards put together with the 'c9x and 86c01.  If
 *   they have different ID's from the ones on the 3500 series machines, 
 *   you can add them here and hopefully things will work out.
 */
			
#define MCA_53C9X_IDS {          \
                         0x7F4C, \
			 0x0000, \
                        }

static int  dma_bytes_sent(struct NCR_ESP *, int);
static int  dma_can_transfer(struct NCR_ESP *, Scsi_Cmnd *);
static void dma_dump_state(struct NCR_ESP *);
static void dma_init_read(struct NCR_ESP *, __u32, int);
static void dma_init_write(struct NCR_ESP *, __u32, int);
static void dma_ints_off(struct NCR_ESP *);
static void dma_ints_on(struct NCR_ESP *);
static int  dma_irq_p(struct NCR_ESP *);
static int  dma_ports_p(struct NCR_ESP *);
static void dma_setup(struct NCR_ESP *, __u32, int, int);
static void dma_led_on(struct NCR_ESP *);
static void dma_led_off(struct NCR_ESP *);

/* This is where all commands are put before they are trasfered to the 
 *  53c9x via PIO.
 */

static volatile unsigned char cmd_buffer[16];

/*
 * We keep the structure that is used to access the registers on the 53c9x
 *  here.
 */

static struct ESP_regs eregs;

/***************************************************************** Detection */
static int mca_esp_detect(struct scsi_host_template *tpnt)
{
	struct NCR_ESP *esp;
	static int io_port_by_pos[] = MCA_53C9X_IO_PORTS;
	int mca_53c9x_ids[] = MCA_53C9X_IDS;
	int *id_to_check = mca_53c9x_ids;
	int slot;
	int pos[3];
	unsigned int tmp_io_addr;
	unsigned char tmp_byte;


	if (!MCA_bus)
		return 0;

	while (*id_to_check) { 
		if ((slot = mca_find_adapter(*id_to_check, 0)) !=
		  MCA_NOTFOUND) 
		{
			esp = esp_allocate(tpnt, (void *) NULL);

			pos[0] = mca_read_stored_pos(slot, 2);
			pos[1] = mca_read_stored_pos(slot, 3);
			pos[2] = mca_read_stored_pos(slot, 4);

			esp->eregs = &eregs;

			/*
			 * IO port base is given in the first (non-ID) pos
			 *  register, like so:
			 *
			 *  Bits 3  2  1       IO base
			 * ----------------------------
			 *       0  0  0       <disabled>
			 *       0  0  1       0x0240
			 *       0  1  0       0x0340
			 *       0  1  1       0x0400
			 *       1  0  0       0x0420
			 *       1  0  1       0x3240
			 *       1  1  0       0x8240
			 *       1  1  1       0xA240
			 */

			tmp_io_addr =
			  io_port_by_pos[(pos[0] & 0x0E) >> 1];

			esp->eregs->io_addr = tmp_io_addr + 0x10;

      			if (esp->eregs->io_addr == 0x0000) { 
        			printk("Adapter is disabled.\n");
				break;
			}

			/*
			 * IRQ is specified in bits 4 and 5:
			 *
			 *  Bits  4  5        IRQ
			 * -----------------------
			 *        0  0         3
			 *        0  1         5
			 *        1  0         7
			 *        1  1         9
			 */

      			esp->irq = ((pos[0] & 0x30) >> 3) + 3;

			/*
			 * DMA channel is in the low 3 bits of the second
			 *  POS register
			 */

			esp->dma = pos[1] & 7;
			esp->slot = slot;

			if (request_irq(esp->irq, esp_intr, 0,
			 "NCR 53c9x SCSI", esp->ehost))
			{
				printk("Unable to request IRQ %d.\n", esp->irq);
				esp_deallocate(esp);
				scsi_unregister(esp->ehost);
				return 0;
			}

 			if (request_dma(esp->dma, "NCR 53c9x SCSI")) {
				printk("Unable to request DMA channel %d.\n",
				 esp->dma);
				free_irq(esp->irq, esp_intr);
				esp_deallocate(esp);
				scsi_unregister(esp->ehost);
				return 0;
			}

			request_region(tmp_io_addr, 32, "NCR 53c9x SCSI");

			/*
			 * 86C01 handles DMA, IO mode, from address
			 *  (base + 0x0a)
			 */

			mca_disable_dma(esp->dma);
			mca_set_dma_io(esp->dma, tmp_io_addr + 0x0a);
			mca_enable_dma(esp->dma);
 
			/* Tell the 86C01 to give us interrupts */

			tmp_byte = inb(tmp_io_addr + 0x02) | 0x40;
			outb(tmp_byte, tmp_io_addr + 0x02); 

			/*
			 * Scsi ID -- general purpose register, hi
			 *  2 bits; add 4 to this number to get the
			 *  ID
			 */

			esp->scsi_id = ((pos[2] & 0xC0) >> 6) + 4;

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

			esp->dma_barrier = NULL;
			esp->dma_drain = NULL;
			esp->dma_invalidate = NULL;
			esp->dma_irq_entry = NULL;
			esp->dma_irq_exit = NULL;
			esp->dma_led_on = dma_led_on;
			esp->dma_led_off = dma_led_off;
			esp->dma_poll = NULL;
			esp->dma_reset = NULL;

			/* Set the command buffer */

			esp->esp_command = (volatile unsigned char*)
			  cmd_buffer;
	 		esp->esp_command_dvma = isa_virt_to_bus(cmd_buffer);

			/* SCSI chip speed */

			esp->cfreq = 25000000;

			/* Differential SCSI? I think not. */

			esp->diff = 0;

			esp_initialize(esp);

      			printk(" Adapter found in slot %2d: io port 0x%x "
			  "irq %d dma channel %d\n", slot + 1, tmp_io_addr,
			   esp->irq, esp->dma);

			mca_set_adapter_name(slot, "NCR 53C9X SCSI Adapter");
			mca_mark_as_used(slot);

			break;
		}
    
		id_to_check++;
	}

	return esps_in_use;
}


/******************************************************************* Release */

static int mca_esp_release(struct Scsi_Host *host)
{
	struct NCR_ESP *esp = (struct NCR_ESP *)host->hostdata;
	unsigned char tmp_byte;

	esp_deallocate(esp);
	/*
	 * Tell the 86C01 to stop sending interrupts
	 */

	tmp_byte = inb(esp->eregs->io_addr - 0x0E);
	tmp_byte &= ~0x40;
	outb(tmp_byte, esp->eregs->io_addr - 0x0E);

	free_irq(esp->irq, esp_intr);
	free_dma(esp->dma);

	mca_mark_as_unused(esp->slot);

	return 0;
}

/************************************************************* DMA Functions */
static int dma_bytes_sent(struct NCR_ESP *esp, int fifo_count)
{
	/* Ask the 53c9x.  It knows. */

	return fifo_count;
}

static int dma_can_transfer(struct NCR_ESP *esp, Scsi_Cmnd *sp)
{
	/* 
	 * The MCA dma channels can only do up to 128K bytes at a time.
         *  (16 bit mode)
	 */

	unsigned long sz = sp->SCp.this_residual;
	if(sz > 0x20000)
		sz = 0x20000;
	return sz;
}

static void dma_dump_state(struct NCR_ESP *esp)
{
	/*
	 * Doesn't quite match up to the other drivers, but we do what we
	 *  can.
	 */

	ESPLOG(("esp%d: dma channel <%d>\n", esp->esp_id, esp->dma));
	ESPLOG(("bytes left to dma: %d\n", mca_get_dma_residue(esp->dma)));
}

static void dma_init_read(struct NCR_ESP *esp, __u32 addr, int length)
{
	unsigned long flags;


	save_flags(flags);
	cli();

	mca_disable_dma(esp->dma);
	mca_set_dma_mode(esp->dma, MCA_DMA_MODE_XFER | MCA_DMA_MODE_16 |
	  MCA_DMA_MODE_IO);
	mca_set_dma_addr(esp->dma, addr);
	mca_set_dma_count(esp->dma, length / 2); /* !!! */
	mca_enable_dma(esp->dma);

	restore_flags(flags);
}

static void dma_init_write(struct NCR_ESP *esp, __u32 addr, int length)
{
	unsigned long flags;


	save_flags(flags);
	cli();

	mca_disable_dma(esp->dma);
	mca_set_dma_mode(esp->dma, MCA_DMA_MODE_XFER | MCA_DMA_MODE_WRITE |
	  MCA_DMA_MODE_16 | MCA_DMA_MODE_IO);
	mca_set_dma_addr(esp->dma, addr);
	mca_set_dma_count(esp->dma, length / 2); /* !!! */
	mca_enable_dma(esp->dma);

	restore_flags(flags);
}

static void dma_ints_off(struct NCR_ESP *esp)
{
	/*
	 * Tell the 'C01 to shut up.  All interrupts are routed through it.
	 */

	outb(inb(esp->eregs->io_addr - 0x0E) & ~0x40,
	 esp->eregs->io_addr - 0x0E);
}

static void dma_ints_on(struct NCR_ESP *esp)
{
	/*
	 * Ok.  You can speak again.
	 */

	outb(inb(esp->eregs->io_addr - 0x0E) | 0x40,
	 esp->eregs->io_addr - 0x0E);
}

static int dma_irq_p(struct NCR_ESP *esp)
{
	/*
	 * DaveM says that this should return a "yes" if there is an interrupt
	 *  or a DMA error occurred.  I copied the Amiga driver's semantics,
	 *  though, because it seems to work and we can't really tell if
	 *  a DMA error happened.  This gives the "yes" if the scsi chip
	 *  is sending an interrupt and no DMA activity is taking place
	 */

	return (!(inb(esp->eregs->io_addr - 0x04) & 1) &&
	 !(inb(esp->eregs->io_addr - 0x04) & 2) ); 
}

static int dma_ports_p(struct NCR_ESP *esp)
{
	/*
	 * Check to see if interrupts are enabled on the 'C01 (in case abort
	 *  is entered multiple times, so we only do the abort once)
	 */

	return (inb(esp->eregs->io_addr - 0x0E) & 0x40) ? 1:0;
}

static void dma_setup(struct NCR_ESP *esp, __u32 addr, int count, int write)
{
	if(write){
		dma_init_write(esp, addr, count);
	} else {
		dma_init_read(esp, addr, count);
	}
}

/*
 * These will not play nicely with other disk controllers that try to use the
 *  disk active LED... but what can you do?  Don't answer that.
 *
 * Stolen shamelessly from ibmmca.c -- IBM Microchannel SCSI adapter driver
 *
 */

static void dma_led_on(struct NCR_ESP *esp)
{
	outb(inb(PS2_SYS_CTR) | 0xc0, PS2_SYS_CTR);
}

static void dma_led_off(struct NCR_ESP *esp)
{
	outb(inb(PS2_SYS_CTR) & 0x3f, PS2_SYS_CTR);
}

static struct scsi_host_template driver_template = {
	.proc_name		= "mca_53c9x",
	.name			= "NCR 53c9x SCSI",
	.detect			= mca_esp_detect,
	.slave_alloc		= esp_slave_alloc,
	.slave_destroy		= esp_slave_destroy,
	.release		= mca_esp_release,
	.queuecommand		= esp_queue,
	.eh_abort_handler	= esp_abort,
	.eh_bus_reset_handler	= esp_reset,
	.can_queue		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 1,
	.unchecked_isa_dma	= 1,
	.use_clustering		= DISABLE_CLUSTERING
};


#include "scsi_module.c"

/*
 * OK, here's the goods I promised.  The NCR 86C01 is an MCA interface chip 
 *  that handles enabling/diabling IRQ, dma interfacing, IO port selection
 *  and other fun stuff.  It takes up 16 addresses, and the chip it is
 *  connnected to gets the following 16.  Registers are as follows:
 *
 * Offsets 0-1 : Card ID
 *
 * Offset    2 : Mode enable register --
 *                Bit    7 : Data Word width (1 = 16, 0 = 8)
 *		  Bit    6 : IRQ enable (1 = enabled)
 *                Bits 5,4 : IRQ select
 *                              0  0 : IRQ 3
 *			        0  1 : IRQ 5
 * 				1  0 : IRQ 7
 *  				1  1 : IRQ 9
 *                Bits 3-1 : Base Address
 *                           0  0  0 : <disabled>
 * 			     0  0  1 : 0x0240
 *    			     0  1  0 : 0x0340
 *     			     0  1  1 : 0x0400
 * 			     1  0  0 : 0x0420
 * 			     1  0  1 : 0x3240
 * 			     1  1  0 : 0x8240
 * 			     1  1  1 : 0xA240
 *		  Bit    0 : Card enable (1 = enabled)
 *
 * Offset    3 : DMA control register --
 *                Bit    7 : DMA enable (1 = enabled)
 *                Bits 6,5 : Preemt Count Select (transfers to complete after
 *                            'C01 has been preempted on MCA bus)
 *                              0  0 : 0
 *                              0  1 : 1
 *                              1  0 : 3
 *                              1  1 : 7
 *  (all these wacky numbers; I'm sure there's a reason somewhere)
 *                Bit    4 : Fairness enable (1 = fair bus priority)
 *                Bits 3-0 : Arbitration level (0-15 consecutive)
 * 
 * Offset    4 : General purpose register
 *                Bits 7-3 : User definable (here, 7,6 are SCSI ID)
 *                Bits 2-0 : reserved
 *
 * Offset   10 : DMA decode register (used for IO based DMA; also can do
 *                PIO through this port)
 *
 * Offset   12 : Status
 *                Bits 7-2 : reserved
 *                Bit    1 : DMA pending (1 = pending)
 *                Bit    0 : IRQ pending (0 = pending)
 *
 * Exciting, huh?  
 *
 */                
