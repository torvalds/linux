/*
 * 68k mac 53c9[46] scsi driver
 *
 * copyright (c) 1998, David Weis weisd3458@uni.edu
 *
 * debugging on Quadra 800 and 660AV Michael Schmitz, Dave Kilzer 7/98
 *
 * based loosely on cyber_esp.c
 */

/* these are unused for now */
#define myreadl(addr) (*(volatile unsigned int *) (addr))
#define mywritel(b, addr) ((*(volatile unsigned int *) (addr)) = (b))


#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "NCR53C9x.h"

#include <asm/io.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/macints.h>
#include <asm/machw.h>
#include <asm/mac_via.h>

#include <asm/pgtable.h>

#include <asm/macintosh.h>

/* #define DEBUG_MAC_ESP */

#define mac_turnon_irq(x)	mac_enable_irq(x)
#define mac_turnoff_irq(x)	mac_disable_irq(x)

extern void esp_handle(struct NCR_ESP *esp);
extern void mac_esp_intr(int irq, void *dev_id, struct pt_regs *pregs);

static int  dma_bytes_sent(struct NCR_ESP * esp, int fifo_count);
static int  dma_can_transfer(struct NCR_ESP * esp, Scsi_Cmnd *sp);
static void dma_dump_state(struct NCR_ESP * esp);
static void dma_init_read(struct NCR_ESP * esp, char * vaddress, int length);
static void dma_init_write(struct NCR_ESP * esp, char * vaddress, int length);
static void dma_ints_off(struct NCR_ESP * esp);
static void dma_ints_on(struct NCR_ESP * esp);
static int  dma_irq_p(struct NCR_ESP * esp);
static int  dma_irq_p_quick(struct NCR_ESP * esp);
static void dma_led_off(struct NCR_ESP * esp);
static void dma_led_on(struct NCR_ESP *esp);
static int  dma_ports_p(struct NCR_ESP *esp);
static void dma_setup(struct NCR_ESP * esp, __u32 addr, int count, int write);
static void dma_setup_quick(struct NCR_ESP * esp, __u32 addr, int count, int write);

static int esp_dafb_dma_irq_p(struct NCR_ESP * espdev);
static int esp_iosb_dma_irq_p(struct NCR_ESP * espdev);

static volatile unsigned char cmd_buffer[16];
				/* This is where all commands are put
				 * before they are transferred to the ESP chip
				 * via PIO.
				 */

static int esp_initialized = 0;

static int setup_num_esps = -1;
static int setup_disconnect = -1;
static int setup_nosync = -1;
static int setup_can_queue = -1;
static int setup_cmd_per_lun = -1;
static int setup_sg_tablesize = -1;
#ifdef SUPPORT_TAGS
static int setup_use_tagged_queuing = -1;
#endif
static int setup_hostid = -1;

/*
 * Experimental ESP inthandler; check macints.c to make sure dev_id is 
 * set up properly!
 */

void mac_esp_intr(int irq, void *dev_id, struct pt_regs *pregs)
{
	struct NCR_ESP *esp = (struct NCR_ESP *) dev_id;
	int irq_p = 0;

	/* Handle the one ESP interrupt showing at this IRQ level. */
	if(((esp)->irq & 0xff) == irq) {
	/*
	 * Debug ..
	 */
		irq_p = esp->dma_irq_p(esp);
	 	printk("mac_esp: irq_p %x current %p disconnected %p\n",
	 		irq_p, esp->current_SC, esp->disconnected_SC);
	 		
		/*
		 * Mac: if we're here, it's an ESP interrupt for sure!
		 */
		if((esp->current_SC || esp->disconnected_SC)) {
			esp->dma_ints_off(esp);

			ESPIRQ(("I%d(", esp->esp_id));
			esp_handle(esp);
			ESPIRQ((")"));

			esp->dma_ints_on(esp);
		}
	}
}

/*
 * Debug hooks; use for playing with the interrupt flag testing and interrupt
 * acknowledge on the various machines
 */

void scsi_esp_polled(int irq, void *dev_id, struct pt_regs *pregs)
{
	if (esp_initialized == 0)
		return;

	mac_esp_intr(irq, dev_id, pregs);
}

void fake_intr(int irq, void *dev_id, struct pt_regs *pregs)
{
#ifdef DEBUG_MAC_ESP
	printk("mac_esp: got irq\n");
#endif

	mac_esp_intr(irq, dev_id, pregs);
}

irqreturn_t fake_drq(int irq, void *dev_id, struct pt_regs *pregs)
{
	printk("mac_esp: got drq\n");
	return IRQ_HANDLED;
}

#define DRIVER_SETUP

/*
 * Function : mac_esp_setup(char *str)
 *
 * Purpose : booter command line initialization of the overrides array,
 *
 * Inputs : str - parameters, separated by commas.
 *
 * Currently unused in the new driver; need to add settable parameters to the 
 * detect function.
 *
 */

static int __init mac_esp_setup(char *str) {
#ifdef DRIVER_SETUP
	/* Format of mac53c9x parameter is:
	 *   mac53c9x=<num_esps>,<disconnect>,<nosync>,<can_queue>,<cmd_per_lun>,<sg_tablesize>,<hostid>,<use_tags>
	 * Negative values mean don't change.
	 */
	
	char *this_opt;
	long opt;

	this_opt = strsep (&str, ",");
	if(this_opt) {
		opt = simple_strtol( this_opt, NULL, 0 );

		if (opt >= 0 && opt <= 2)
			setup_num_esps = opt;
		else if (opt > 2)
			printk( "mac_esp_setup: invalid number of hosts %ld !\n", opt );

		this_opt = strsep (&str, ",");
	}
	if(this_opt) {
		opt = simple_strtol( this_opt, NULL, 0 );
	
		if (opt > 0)
			setup_disconnect = opt;

		this_opt = strsep (&str, ",");
	}
	if(this_opt) {
		opt = simple_strtol( this_opt, NULL, 0 );

		if (opt >= 0)
			setup_nosync = opt;

		this_opt = strsep (&str, ",");
	}
	if(this_opt) {
		opt = simple_strtol( this_opt, NULL, 0 );

		if (opt > 0)
			setup_can_queue = opt;

		this_opt = strsep (&str, ",");
	}
	if(this_opt) {
		opt = simple_strtol( this_opt, NULL, 0 );

		if (opt > 0)
			setup_cmd_per_lun = opt;

		this_opt = strsep (&str, ",");
	}
	if(this_opt) {
		opt = simple_strtol( this_opt, NULL, 0 );

		if (opt >= 0) {
			setup_sg_tablesize = opt;
			/* Must be <= SG_ALL (255) */
			if (setup_sg_tablesize > SG_ALL)
				setup_sg_tablesize = SG_ALL;
		}

		this_opt = strsep (&str, ",");
	}
	if(this_opt) {
		opt = simple_strtol( this_opt, NULL, 0 );

		/* Must be between 0 and 7 */
		if (opt >= 0 && opt <= 7)
			setup_hostid = opt;
		else if (opt > 7)
			printk( "mac_esp_setup: invalid host ID %ld !\n", opt);

		this_opt = strsep (&str, ",");
	}
#ifdef SUPPORT_TAGS
	if(this_opt) {
		opt = simple_strtol( this_opt, NULL, 0 );
		if (opt >= 0)
			setup_use_tagged_queuing = !!opt;
	}
#endif
#endif
	return 1; 
}

__setup("mac53c9x=", mac_esp_setup);


/*
 * ESP address 'detection'
 */

unsigned long get_base(int chip_num)
{
	/*
	 * using the chip_num and mac model, figure out where the
	 * chips are mapped
	 */

	unsigned long io_base = 0x50f00000;
	unsigned int second_offset = 0x402;
	unsigned long scsi_loc = 0;

	switch (macintosh_config->scsi_type) {

	/* 950, 900, 700 */
	case MAC_SCSI_QUADRA2:
		scsi_loc =  io_base + 0xf000 + ((chip_num == 0) ? 0 : second_offset);
		break;

	/* av's */
	case MAC_SCSI_QUADRA3:
		scsi_loc = io_base + 0x18000 + ((chip_num == 0) ? 0 : second_offset);
		break;

	/* most quadra/centris models are like this */	
	case MAC_SCSI_QUADRA:
		scsi_loc = io_base + 0x10000;
		break;

	default:
		printk("mac_esp: get_base: hit default!\n");
		scsi_loc = io_base + 0x10000;
		break;

	} /* switch */

	printk("mac_esp: io base at 0x%lx\n", scsi_loc);

	return scsi_loc;
}

/*
 * Model dependent ESP setup
 */

int mac_esp_detect(struct scsi_host_template * tpnt)
{
	int quick = 0;
	int chipnum, chipspresent = 0;
#if 0
	unsigned long timeout;
#endif

	if (esp_initialized > 0)
		return -ENODEV;

	/* what do we have in this machine... */
	if (MACHW_PRESENT(MAC_SCSI_96)) {
		chipspresent ++;
	}

	if (MACHW_PRESENT(MAC_SCSI_96_2)) {
		chipspresent ++;
	}

	/* number of ESPs present ? */
	if (setup_num_esps >= 0) {
	  if (chipspresent >= setup_num_esps)
	    chipspresent = setup_num_esps;
	  else
	    printk("mac_esp_detect: num_hosts detected %d setup %d \n",
		   chipspresent, setup_num_esps);
	}

	/* TODO: add disconnect / nosync flags */

	/* setup variables */
	tpnt->can_queue =
	  (setup_can_queue > 0) ? setup_can_queue : 7;
	tpnt->cmd_per_lun =
	  (setup_cmd_per_lun > 0) ? setup_cmd_per_lun : 1;
	tpnt->sg_tablesize = 
	  (setup_sg_tablesize >= 0) ? setup_sg_tablesize : SG_ALL;

	if (setup_hostid >= 0)
	  tpnt->this_id = setup_hostid;
	else {
	  /* use 7 as default */
	  tpnt->this_id = 7;
	}

#ifdef SUPPORT_TAGS
	if (setup_use_tagged_queuing < 0)
		setup_use_tagged_queuing = DEFAULT_USE_TAGGED_QUEUING;
#endif

	for (chipnum = 0; chipnum < chipspresent; chipnum ++) {
		struct NCR_ESP * esp;

		esp = esp_allocate(tpnt, (void *) NULL);
		esp->eregs = (struct ESP_regs *) get_base(chipnum);

		esp->dma_irq_p = &esp_dafb_dma_irq_p;
		if (chipnum == 0) {

			if (macintosh_config->scsi_type == MAC_SCSI_QUADRA) {
				/* most machines except those below :-) */
				quick = 1;
				esp->dma_irq_p = &esp_iosb_dma_irq_p;
			} else if (macintosh_config->scsi_type == MAC_SCSI_QUADRA3) {
				/* mostly av's */
				quick = 0;
			} else {
				/* q950, 900, 700 */
				quick = 1;
				out_be32(0xf9800024, 0x1d1);
				esp->dregs = (void *) 0xf9800024;
			}

		} else { /* chipnum */

			quick = 1;
			out_be32(0xf9800028, 0x1d1);
			esp->dregs = (void *) 0xf9800028;

		} /* chipnum == 0 */

		/* use pio for command bytes; pio for message/data: TBI */
		esp->do_pio_cmds = 1;

		/* Set the command buffer */
		esp->esp_command = (volatile unsigned char*) cmd_buffer;
		esp->esp_command_dvma = (__u32) cmd_buffer;

		/* various functions */
		esp->dma_bytes_sent = &dma_bytes_sent;
		esp->dma_can_transfer = &dma_can_transfer;
		esp->dma_dump_state = &dma_dump_state;
		esp->dma_init_read = NULL;
		esp->dma_init_write = NULL;
		esp->dma_ints_off = &dma_ints_off;
		esp->dma_ints_on = &dma_ints_on;

		esp->dma_ports_p = &dma_ports_p;


		/* Optional functions */
		esp->dma_barrier = NULL;
		esp->dma_drain = NULL;
		esp->dma_invalidate = NULL;
		esp->dma_irq_entry = NULL;
		esp->dma_irq_exit = NULL;
		esp->dma_led_on = NULL;
		esp->dma_led_off = NULL;
		esp->dma_poll = NULL;
		esp->dma_reset = NULL;

		/* SCSI chip speed */
		/* below esp->cfreq = 40000000; */


		if (quick) {
			/* 'quick' means there's handshake glue logic like in the 5380 case */
			esp->dma_setup = &dma_setup_quick;
		} else {
			esp->dma_setup = &dma_setup;
		}

		if (chipnum == 0) {

			esp->irq = IRQ_MAC_SCSI;

			request_irq(IRQ_MAC_SCSI, esp_intr, 0, "Mac ESP SCSI", esp->ehost);
#if 0	/* conflicts with IOP ADB */
			request_irq(IRQ_MAC_SCSIDRQ, fake_drq, 0, "Mac ESP DRQ", esp->ehost);
#endif

			if (macintosh_config->scsi_type == MAC_SCSI_QUADRA) {
				esp->cfreq = 16500000;
			} else {
				esp->cfreq = 25000000;
			}


		} else { /* chipnum == 1 */

			esp->irq = IRQ_MAC_SCSIDRQ;
#if 0	/* conflicts with IOP ADB */
			request_irq(IRQ_MAC_SCSIDRQ, esp_intr, 0, "Mac ESP SCSI 2", esp->ehost);
#endif

			esp->cfreq = 25000000;

		}

		if (quick) {
			printk("esp: using quick version\n");
		}

		printk("esp: addr at 0x%p\n", esp->eregs);

		esp->scsi_id = 7;
		esp->diff = 0;

		esp_initialize(esp);

	} /* for chipnum */

	if (chipspresent)
		printk("\nmac_esp: %d esp controllers found\n", chipspresent);

	esp_initialized = chipspresent;

	return chipspresent;
}

static int mac_esp_release(struct Scsi_Host *shost)
{
	if (shost->irq)
		free_irq(shost->irq, NULL);
	if (shost->io_port && shost->n_io_port)
		release_region(shost->io_port, shost->n_io_port);
	scsi_unregister(shost);
	return 0;
}

/*
 * I've been wondering what this is supposed to do, for some time. Talking 
 * to Allen Briggs: These machines have an extra register someplace where the
 * DRQ pin of the ESP can be monitored. That isn't useful for determining 
 * anything else (such as reselect interrupt or other magic) though. 
 * Maybe make the semantics should be changed like 
 * if (esp->current_SC)
 *	... check DRQ flag ...
 * else 
 *	... disconnected, check pending VIA interrupt ...
 *
 * There's a problem with using the dabf flag or mac_irq_pending() here: both
 * seem to return 1 even though no interrupt is currently pending, resulting
 * in esp_exec_cmd() holding off the next command, and possibly infinite loops
 * in esp_intr(). 
 * Short term fix: just use esp_status & ESP_STAT_INTR here, as long as we
 * use simple PIO. The DRQ status will be important when implementing pseudo
 * DMA mode (set up ESP transfer count, return, do a batch of bytes in PIO or 
 * 'hardware handshake' mode upon DRQ).
 * If you plan on changing this (i.e. to save the esp_status register access in 
 * favor of a VIA register access or a shadow register for the IFR), make sure
 * to try a debug version of this first to monitor what registers would be a good
 * indicator of the ESP interrupt.
 */

static int esp_dafb_dma_irq_p(struct NCR_ESP * esp)
{
	unsigned int ret;
	int sreg = esp_read(esp->eregs->esp_status);

#ifdef DEBUG_MAC_ESP
	printk("mac_esp: esp_dafb_dma_irq_p dafb %d irq %d\n", 
		readl(esp->dregs), mac_irq_pending(IRQ_MAC_SCSI));
#endif

	sreg &= ESP_STAT_INTR;

	/*
	 * maybe working; this is essentially what's used for iosb_dma_irq_p
	 */
	if (sreg)
		return 1;
	else
		return 0;

	/*
	 * didn't work ...
	 */
#if 0
	if (esp->current_SC)
		ret = readl(esp->dregs) & 0x200;
	else if (esp->disconnected_SC)
		ret = 1; /* sreg ?? */
	else
		ret = mac_irq_pending(IRQ_MAC_SCSI);

	return(ret);
#endif

}

/*
 * See above: testing mac_irq_pending always returned 8 (SCSI IRQ) regardless 
 * of the actual ESP status.
 */

static int esp_iosb_dma_irq_p(struct NCR_ESP * esp)
{
	int ret  = mac_irq_pending(IRQ_MAC_SCSI) || mac_irq_pending(IRQ_MAC_SCSIDRQ);
	int sreg = esp_read(esp->eregs->esp_status);

#ifdef DEBUG_MAC_ESP
	printk("mac_esp: dma_irq_p drq %d irq %d sreg %x curr %p disc %p\n", 
		mac_irq_pending(IRQ_MAC_SCSIDRQ), mac_irq_pending(IRQ_MAC_SCSI), 
		sreg, esp->current_SC, esp->disconnected_SC);
#endif

	sreg &= ESP_STAT_INTR;

	if (sreg)
		return (sreg);
	else
		return 0;
}

/*
 * This seems to be OK for PIO at least ... usually 0 after PIO.
 */

static int dma_bytes_sent(struct NCR_ESP * esp, int fifo_count)
{

#ifdef DEBUG_MAC_ESP
	printk("mac_esp: dma bytes sent = %x\n", fifo_count);
#endif

	return fifo_count;
}

/*
 * dma_can_transfer is used to switch between DMA and PIO, if DMA (pseudo)
 * is ever implemented. Returning 0 here will use PIO.
 */

static int dma_can_transfer(struct NCR_ESP * esp, Scsi_Cmnd * sp)
{
	unsigned long sz = sp->SCp.this_residual;
#if 0	/* no DMA yet; make conditional */
	if (sz > 0x10000000) {
		sz = 0x10000000;
	}
	printk("mac_esp: dma can transfer = 0lx%x\n", sz);
#else

#ifdef DEBUG_MAC_ESP
	printk("mac_esp: pio to transfer = %ld\n", sz);
#endif

	sz = 0;
#endif
	return sz;
}

/*
 * Not yet ...
 */

static void dma_dump_state(struct NCR_ESP * esp)
{
#ifdef DEBUG_MAC_ESP
	printk("mac_esp: dma_dump_state: called\n");
#endif
#if 0
	ESPLOG(("esp%d: dma -- cond_reg<%02x>\n",
		esp->esp_id, ((struct mac_dma_registers *)
		(esp->dregs))->cond_reg));
#endif
}

/*
 * DMA setup: should be used to set up the ESP transfer count for pseudo
 * DMA transfers; need a DRQ transfer function to do the actual transfer
 */

static void dma_init_read(struct NCR_ESP * esp, char * vaddress, int length)
{
	printk("mac_esp: dma_init_read\n");
}


static void dma_init_write(struct NCR_ESP * esp, char * vaddress, int length)
{
	printk("mac_esp: dma_init_write\n");
}


static void dma_ints_off(struct NCR_ESP * esp)
{
	mac_turnoff_irq(esp->irq);
}


static void dma_ints_on(struct NCR_ESP * esp)
{
	mac_turnon_irq(esp->irq);
}

/*
 * generic dma_irq_p(), unused
 */

static int dma_irq_p(struct NCR_ESP * esp)
{
	int i = esp_read(esp->eregs->esp_status);

#ifdef DEBUG_MAC_ESP
	printk("mac_esp: dma_irq_p status %d\n", i);
#endif

	return (i & ESP_STAT_INTR);
}

static int dma_irq_p_quick(struct NCR_ESP * esp)
{
	/*
	 * Copied from iosb_dma_irq_p()
	 */
	int ret  = mac_irq_pending(IRQ_MAC_SCSI) || mac_irq_pending(IRQ_MAC_SCSIDRQ);
	int sreg = esp_read(esp->eregs->esp_status);

#ifdef DEBUG_MAC_ESP
	printk("mac_esp: dma_irq_p drq %d irq %d sreg %x curr %p disc %p\n", 
		mac_irq_pending(IRQ_MAC_SCSIDRQ), mac_irq_pending(IRQ_MAC_SCSI), 
		sreg, esp->current_SC, esp->disconnected_SC);
#endif

	sreg &= ESP_STAT_INTR;

	if (sreg)
		return (sreg);
	else
		return 0;

}

static void dma_led_off(struct NCR_ESP * esp)
{
#ifdef DEBUG_MAC_ESP
	printk("mac_esp: dma_led_off: called\n");
#endif
}


static void dma_led_on(struct NCR_ESP * esp)
{
#ifdef DEBUG_MAC_ESP
	printk("mac_esp: dma_led_on: called\n");
#endif
}


static int dma_ports_p(struct NCR_ESP * esp)
{
	return 0;
}


static void dma_setup(struct NCR_ESP * esp, __u32 addr, int count, int write)
{

#ifdef DEBUG_MAC_ESP
	printk("mac_esp: dma_setup\n");
#endif

	if (write) {
		dma_init_read(esp, (char *) addr, count);
	} else {
		dma_init_write(esp, (char *) addr, count);
	}
}


static void dma_setup_quick(struct NCR_ESP * esp, __u32 addr, int count, int write)
{
#ifdef DEBUG_MAC_ESP
	printk("mac_esp: dma_setup_quick\n");
#endif
}

static struct scsi_host_template driver_template = {
	.proc_name		= "mac_esp",
	.name			= "Mac 53C9x SCSI",
	.detect			= mac_esp_detect,
	.slave_alloc		= esp_slave_alloc,
	.slave_destroy		= esp_slave_destroy,
	.release		= mac_esp_release,
	.info			= esp_info,
	.queuecommand		= esp_queue,
	.eh_abort_handler	= esp_abort,
	.eh_bus_reset_handler	= esp_reset,
	.can_queue		= 7,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 1,
	.use_clustering		= DISABLE_CLUSTERING
};


#include "scsi_module.c"

MODULE_LICENSE("GPL");
