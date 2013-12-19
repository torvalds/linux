/*
 * Sun3 SCSI stuff by Erik Verbruggen (erik@bigmama.xtdnet.nl)
 *
 * Sun3 DMA routines added by Sam Creasey (sammy@sammy.net)
 *
 * Adapted from mac_scsinew.c:
 */
/*
 * Generic Macintosh NCR5380 driver
 *
 * Copyright 1998, Michael Schmitz <mschmitz@lbl.gov>
 *
 * derived in part from:
 */
/*
 * Generic Generic NCR5380 driver
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


/*
 * This is from mac_scsi.h, but hey, maybe this is useful for Sun3 too! :)
 *
 * Options :
 *
 * PARITY - enable parity checking.  Not supported.
 *
 * SCSI2 - enable support for SCSI-II tagged queueing.  Untested.
 *
 * USLEEP - enable support for devices that don't disconnect.  Untested.
 */

/*
 * $Log: sun3_NCR5380.c,v $
 */

#define AUTOSENSE

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/ctype.h>
#include <linux/delay.h>

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/blkdev.h>

#include <asm/io.h>

#include <asm/sun3ints.h>
#include <asm/dvma.h>
#include <asm/idprom.h>
#include <asm/machines.h>

#define NDEBUG 0

#define NDEBUG_ABORT		0x00100000
#define NDEBUG_TAGS		0x00200000
#define NDEBUG_MERGING		0x00400000

/* dma on! */
#define REAL_DMA

#include "scsi.h"
#include "initio.h"
#include <scsi/scsi_host.h>
#include "sun3_scsi.h"

static void NCR5380_print(struct Scsi_Host *instance);

/* #define OLDDMA */

#define USE_WRAPPER
/*#define RESET_BOOT */
#define DRIVER_SETUP

/*
 * BUG can be used to trigger a strange code-size related hang on 2.1 kernels
 */
#ifdef BUG
#undef RESET_BOOT
#undef DRIVER_SETUP
#endif

/* #define SUPPORT_TAGS */

#define	ENABLE_IRQ()	enable_irq( IRQ_SUN3_SCSI ); 


static irqreturn_t scsi_sun3_intr(int irq, void *dummy);
static inline unsigned char sun3scsi_read(int reg);
static inline void sun3scsi_write(int reg, int value);

static int setup_can_queue = -1;
module_param(setup_can_queue, int, 0);
static int setup_cmd_per_lun = -1;
module_param(setup_cmd_per_lun, int, 0);
static int setup_sg_tablesize = -1;
module_param(setup_sg_tablesize, int, 0);
#ifdef SUPPORT_TAGS
static int setup_use_tagged_queuing = -1;
module_param(setup_use_tagged_queuing, int, 0);
#endif
static int setup_hostid = -1;
module_param(setup_hostid, int, 0);

static struct scsi_cmnd *sun3_dma_setup_done = NULL;

#define	AFTER_RESET_DELAY	(HZ/2)

/* ms to wait after hitting dma regs */
#define SUN3_DMA_DELAY 10

/* dvma buffer to allocate -- 32k should hopefully be more than sufficient */
#define SUN3_DVMA_BUFSIZE 0xe000

/* minimum number of bytes to do dma on */
#define SUN3_DMA_MINSIZE 128

static volatile unsigned char *sun3_scsi_regp;
static volatile struct sun3_dma_regs *dregs;
#ifdef OLDDMA
static unsigned char *dmabuf = NULL; /* dma memory buffer */
#endif
static struct sun3_udc_regs *udc_regs = NULL;
static unsigned char *sun3_dma_orig_addr = NULL;
static unsigned long sun3_dma_orig_count = 0;
static int sun3_dma_active = 0;
static unsigned long last_residual = 0;

/*
 * NCR 5380 register access functions
 */

static inline unsigned char sun3scsi_read(int reg)
{
	return( sun3_scsi_regp[reg] );
}

static inline void sun3scsi_write(int reg, int value)
{
	sun3_scsi_regp[reg] = value;
}

/* dma controller register access functions */

static inline unsigned short sun3_udc_read(unsigned char reg)
{
	unsigned short ret;

	dregs->udc_addr = UDC_CSR;
	udelay(SUN3_DMA_DELAY);
	ret = dregs->udc_data;
	udelay(SUN3_DMA_DELAY);
	
	return ret;
}

static inline void sun3_udc_write(unsigned short val, unsigned char reg)
{
	dregs->udc_addr = reg;
	udelay(SUN3_DMA_DELAY);
	dregs->udc_data = val;
	udelay(SUN3_DMA_DELAY);
}

/*
 * XXX: status debug
 */
static struct Scsi_Host *default_instance;

/*
 * Function : int sun3scsi_detect(struct scsi_host_template * tpnt)
 *
 * Purpose : initializes mac NCR5380 driver based on the
 *	command line / compile time port and irq definitions.
 *
 * Inputs : tpnt - template for this SCSI adapter.
 *
 * Returns : 1 if a host adapter was found, 0 if not.
 *
 */
 
int __init sun3scsi_detect(struct scsi_host_template * tpnt)
{
	unsigned long ioaddr;
	static int called = 0;
	struct Scsi_Host *instance;

	/* check that this machine has an onboard 5380 */
	switch(idprom->id_machtype) {
	case SM_SUN3|SM_3_50:
	case SM_SUN3|SM_3_60:
		break;

	default:
		return 0;
	}

	if(called)
		return 0;

	tpnt->proc_name = "Sun3 5380 SCSI";

	/* setup variables */
	tpnt->can_queue =
		(setup_can_queue > 0) ? setup_can_queue : CAN_QUEUE;
	tpnt->cmd_per_lun =
		(setup_cmd_per_lun > 0) ? setup_cmd_per_lun : CMD_PER_LUN;
	tpnt->sg_tablesize = 
		(setup_sg_tablesize >= 0) ? setup_sg_tablesize : SG_TABLESIZE;

	if (setup_hostid >= 0)
		tpnt->this_id = setup_hostid;
	else {
		/* use 7 as default */
		tpnt->this_id = 7;
	}

	ioaddr = (unsigned long)ioremap(IOBASE_SUN3_SCSI, PAGE_SIZE);
	sun3_scsi_regp = (unsigned char *)ioaddr;

	dregs = (struct sun3_dma_regs *)(((unsigned char *)ioaddr) + 8);

	if((udc_regs = dvma_malloc(sizeof(struct sun3_udc_regs)))
	   == NULL) {
	     printk("SUN3 Scsi couldn't allocate DVMA memory!\n");
	     return 0;
	}
#ifdef OLDDMA
	if((dmabuf = dvma_malloc_align(SUN3_DVMA_BUFSIZE, 0x10000)) == NULL) {
	     printk("SUN3 Scsi couldn't allocate DVMA memory!\n");
	     return 0;
	}
#endif
#ifdef SUPPORT_TAGS
	if (setup_use_tagged_queuing < 0)
		setup_use_tagged_queuing = USE_TAGGED_QUEUING;
#endif

	instance = scsi_register (tpnt, sizeof(struct NCR5380_hostdata));
	if(instance == NULL)
		return 0;
		
	default_instance = instance;

        instance->io_port = (unsigned long) ioaddr;
	instance->irq = IRQ_SUN3_SCSI;

	NCR5380_init(instance, 0);

	instance->n_io_port = 32;

        ((struct NCR5380_hostdata *)instance->hostdata)->ctrl = 0;

	if (request_irq(instance->irq, scsi_sun3_intr,
			     0, "Sun3SCSI-5380", instance)) {
#ifndef REAL_DMA
		printk("scsi%d: IRQ%d not free, interrupts disabled\n",
		       instance->host_no, instance->irq);
		instance->irq = SCSI_IRQ_NONE;
#else
		printk("scsi%d: IRQ%d not free, bailing out\n",
		       instance->host_no, instance->irq);
		return 0;
#endif
	}
	
	printk("scsi%d: Sun3 5380 at port %lX irq", instance->host_no, instance->io_port);
	if (instance->irq == SCSI_IRQ_NONE)
		printk ("s disabled");
	else
		printk (" %d", instance->irq);
	printk(" options CAN_QUEUE=%d CMD_PER_LUN=%d release=%d",
	       instance->can_queue, instance->cmd_per_lun,
	       SUN3SCSI_PUBLIC_RELEASE);
	printk("\nscsi%d:", instance->host_no);
	NCR5380_print_options(instance);
	printk("\n");

	dregs->csr = 0;
	udelay(SUN3_DMA_DELAY);
	dregs->csr = CSR_SCSI | CSR_FIFO | CSR_INTR;
	udelay(SUN3_DMA_DELAY);
	dregs->fifo_count = 0;

	called = 1;

#ifdef RESET_BOOT
	sun3_scsi_reset_boot(instance);
#endif

	return 1;
}

int sun3scsi_release (struct Scsi_Host *shpnt)
{
	if (shpnt->irq != SCSI_IRQ_NONE)
		free_irq(shpnt->irq, shpnt);

	iounmap((void *)sun3_scsi_regp);

	NCR5380_exit(shpnt);
	return 0;
}

#ifdef RESET_BOOT
/*
 * Our 'bus reset on boot' function
 */

static void sun3_scsi_reset_boot(struct Scsi_Host *instance)
{
	unsigned long end;

	NCR5380_local_declare();
	NCR5380_setup(instance);
	
	/*
	 * Do a SCSI reset to clean up the bus during initialization. No
	 * messing with the queues, interrupts, or locks necessary here.
	 */

	printk( "Sun3 SCSI: resetting the SCSI bus..." );

	/* switch off SCSI IRQ - catch an interrupt without IRQ bit set else */
//       	sun3_disable_irq( IRQ_SUN3_SCSI );

	/* get in phase */
	NCR5380_write( TARGET_COMMAND_REG,
		      PHASE_SR_TO_TCR( NCR5380_read(STATUS_REG) ));

	/* assert RST */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_RST );

	/* The min. reset hold time is 25us, so 40us should be enough */
	udelay( 50 );

	/* reset RST and interrupt */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE );
	NCR5380_read( RESET_PARITY_INTERRUPT_REG );

	for( end = jiffies + AFTER_RESET_DELAY; time_before(jiffies, end); )
		barrier();

	/* switch on SCSI IRQ again */
//       	sun3_enable_irq( IRQ_SUN3_SCSI );

	printk( " done\n" );
}
#endif

const char * sun3scsi_info (struct Scsi_Host *spnt) {
    return "";
}

// safe bits for the CSR
#define CSR_GOOD 0x060f

static irqreturn_t scsi_sun3_intr(int irq, void *dummy)
{
	unsigned short csr = dregs->csr;
	int handled = 0;

	if(csr & ~CSR_GOOD) {
		if(csr & CSR_DMA_BUSERR) {
			printk("scsi%d: bus error in dma\n", default_instance->host_no);
		}

		if(csr & CSR_DMA_CONFLICT) {
			printk("scsi%d: dma conflict\n", default_instance->host_no);
		}
		handled = 1;
	}

	if(csr & (CSR_SDB_INT | CSR_DMA_INT)) {
		NCR5380_intr(irq, dummy);
		handled = 1;
	}

	return IRQ_RETVAL(handled);
}

/*
 * Debug stuff - to be called on NMI, or sysrq key. Use at your own risk; 
 * reentering NCR5380_print_status seems to have ugly side effects
 */

/* this doesn't seem to get used at all -- sam */
#if 0
void sun3_sun3_debug (void)
{
	unsigned long flags;
	NCR5380_local_declare();

	if (default_instance) {
			local_irq_save(flags);
			NCR5380_print_status(default_instance);
			local_irq_restore(flags);
	}
}
#endif


/* sun3scsi_dma_setup() -- initialize the dma controller for a read/write */
static unsigned long sun3scsi_dma_setup(void *data, unsigned long count, int write_flag)
{
#ifdef OLDDMA
	if(write_flag) 
		memcpy(dmabuf, data, count);
	else {
		sun3_dma_orig_addr = data;
		sun3_dma_orig_count = count;
	}
#else
	void *addr;

	if(sun3_dma_orig_addr != NULL)
		dvma_unmap(sun3_dma_orig_addr);

//	addr = sun3_dvma_page((unsigned long)data, (unsigned long)dmabuf);
	addr = (void *)dvma_map((unsigned long) data, count);
		
	sun3_dma_orig_addr = addr;
	sun3_dma_orig_count = count;
#endif
	dregs->fifo_count = 0;
	sun3_udc_write(UDC_RESET, UDC_CSR);
	
	/* reset fifo */
	dregs->csr &= ~CSR_FIFO;
	dregs->csr |= CSR_FIFO;
	
	/* set direction */
	if(write_flag)
		dregs->csr |= CSR_SEND;
	else
		dregs->csr &= ~CSR_SEND;
	
	/* byte count for fifo */
	dregs->fifo_count = count;

	sun3_udc_write(UDC_RESET, UDC_CSR);
	
	/* reset fifo */
	dregs->csr &= ~CSR_FIFO;
	dregs->csr |= CSR_FIFO;
	
	if(dregs->fifo_count != count) { 
		printk("scsi%d: fifo_mismatch %04x not %04x\n",
		       default_instance->host_no, dregs->fifo_count,
		       (unsigned int) count);
		NCR5380_print(default_instance);
	}

	/* setup udc */
#ifdef OLDDMA
	udc_regs->addr_hi = ((dvma_vtob(dmabuf) & 0xff0000) >> 8);
	udc_regs->addr_lo = (dvma_vtob(dmabuf) & 0xffff);
#else
	udc_regs->addr_hi = (((unsigned long)(addr) & 0xff0000) >> 8);
	udc_regs->addr_lo = ((unsigned long)(addr) & 0xffff);
#endif
	udc_regs->count = count/2; /* count in words */
	udc_regs->mode_hi = UDC_MODE_HIWORD;
	if(write_flag) {
		if(count & 1)
			udc_regs->count++;
		udc_regs->mode_lo = UDC_MODE_LSEND;
		udc_regs->rsel = UDC_RSEL_SEND;
	} else {
		udc_regs->mode_lo = UDC_MODE_LRECV;
		udc_regs->rsel = UDC_RSEL_RECV;
	}
	
	/* announce location of regs block */
	sun3_udc_write(((dvma_vtob(udc_regs) & 0xff0000) >> 8),
		       UDC_CHN_HI); 

	sun3_udc_write((dvma_vtob(udc_regs) & 0xffff), UDC_CHN_LO);

	/* set dma master on */
	sun3_udc_write(0xd, UDC_MODE);

	/* interrupt enable */
	sun3_udc_write(UDC_INT_ENABLE, UDC_CSR);
	
       	return count;

}

static inline unsigned long sun3scsi_dma_count(struct Scsi_Host *instance)
{
	unsigned short resid;

	dregs->udc_addr = 0x32; 
	udelay(SUN3_DMA_DELAY);
	resid = dregs->udc_data;
	udelay(SUN3_DMA_DELAY);
	resid *= 2;

	return (unsigned long) resid;
}

static inline unsigned long sun3scsi_dma_residual(struct Scsi_Host *instance)
{
	return last_residual;
}

static inline unsigned long sun3scsi_dma_xfer_len(unsigned long wanted,
						  struct scsi_cmnd *cmd,
						  int write_flag)
{
	if (cmd->request->cmd_type == REQ_TYPE_FS)
 		return wanted;
	else
		return 0;
}

static inline int sun3scsi_dma_start(unsigned long count, unsigned char *data)
{

    sun3_udc_write(UDC_CHN_START, UDC_CSR);
    
    return 0;
}

/* clean up after our dma is done */
static int sun3scsi_dma_finish(int write_flag)
{
	unsigned short count;
	unsigned short fifo;
	int ret = 0;
	
	sun3_dma_active = 0;
#if 1
	// check to empty the fifo on a read
	if(!write_flag) {
		int tmo = 20000; /* .2 sec */
		
		while(1) {
			if(dregs->csr & CSR_FIFO_EMPTY)
				break;

			if(--tmo <= 0) {
				printk("sun3scsi: fifo failed to empty!\n");
				return 1;
			}
			udelay(10);
		}
	}
		
#endif

	count = sun3scsi_dma_count(default_instance);
#ifdef OLDDMA

	/* if we've finished a read, copy out the data we read */
 	if(sun3_dma_orig_addr) {
		/* check for residual bytes after dma end */
		if(count && (NCR5380_read(BUS_AND_STATUS_REG) &
			     (BASR_PHASE_MATCH | BASR_ACK))) {
			printk("scsi%d: sun3_scsi_finish: read overrun baby... ", default_instance->host_no);
			printk("basr now %02x\n", NCR5380_read(BUS_AND_STATUS_REG));
			ret = count;
		}
		
		/* copy in what we dma'd no matter what */
		memcpy(sun3_dma_orig_addr, dmabuf, sun3_dma_orig_count);
		sun3_dma_orig_addr = NULL;

	}
#else

	fifo = dregs->fifo_count;
	last_residual = fifo;

	/* empty bytes from the fifo which didn't make it */
	if((!write_flag) && (count - fifo) == 2) {
		unsigned short data;
		unsigned char *vaddr;

		data = dregs->fifo_data;
		vaddr = (unsigned char *)dvma_btov(sun3_dma_orig_addr);
		
		vaddr += (sun3_dma_orig_count - fifo);

		vaddr[-2] = (data & 0xff00) >> 8;
		vaddr[-1] = (data & 0xff);
	}

	dvma_unmap(sun3_dma_orig_addr);
	sun3_dma_orig_addr = NULL;
#endif
	sun3_udc_write(UDC_RESET, UDC_CSR);
	dregs->fifo_count = 0;
	dregs->csr &= ~CSR_SEND;

	/* reset fifo */
	dregs->csr &= ~CSR_FIFO;
	dregs->csr |= CSR_FIFO;
	
	sun3_dma_setup_done = NULL;

	return ret;

}
	
#include "sun3_NCR5380.c"

static struct scsi_host_template driver_template = {
	.show_info		= sun3scsi_show_info,
	.name			= SUN3_SCSI_NAME,
	.detect			= sun3scsi_detect,
	.release		= sun3scsi_release,
	.info			= sun3scsi_info,
	.queuecommand		= sun3scsi_queue_command,
	.eh_abort_handler      	= sun3scsi_abort,
	.eh_bus_reset_handler  	= sun3scsi_bus_reset,
	.can_queue		= CAN_QUEUE,
	.this_id		= 7,
	.sg_tablesize		= SG_TABLESIZE,
	.cmd_per_lun		= CMD_PER_LUN,
	.use_clustering		= DISABLE_CLUSTERING
};


#include "scsi_module.c"

MODULE_LICENSE("GPL");
