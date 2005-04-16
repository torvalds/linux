 /*
 * Sun3 SCSI stuff by Erik Verbruggen (erik@bigmama.xtdnet.nl)
 *
 * Sun3 DMA routines added by Sam Creasey (sammy@sammy.net)
 *
 * VME support added by Sam Creasey
 *
 * Adapted from sun3_scsi.c -- see there for other headers
 *
 * TODO: modify this driver to support multiple Sun3 SCSI VME boards
 *
 */

#define AUTOSENSE

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/ctype.h>
#include <linux/delay.h>

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/blkdev.h>

#include <asm/io.h>
#include <asm/system.h>

#include <asm/sun3ints.h>
#include <asm/dvma.h>
#include <asm/idprom.h>
#include <asm/machines.h>

#define SUN3_SCSI_VME

#undef SUN3_SCSI_DEBUG

/* dma on! */
#define REAL_DMA

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "sun3_scsi.h"
#include "NCR5380.h"

extern int sun3_map_test(unsigned long, char *);

#define USE_WRAPPER
/*#define RESET_BOOT */
#define DRIVER_SETUP

#define NDEBUG 0

/*
 * BUG can be used to trigger a strange code-size related hang on 2.1 kernels
 */
#ifdef BUG
#undef RESET_BOOT
#undef DRIVER_SETUP
#endif

/* #define SUPPORT_TAGS */

//#define	ENABLE_IRQ()	enable_irq( SUN3_VEC_VMESCSI0 ); 
#define ENABLE_IRQ()


static irqreturn_t scsi_sun3_intr(int irq, void *dummy, struct pt_regs *fp);
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

static Scsi_Cmnd *sun3_dma_setup_done = NULL;

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

/*
 * XXX: status debug
 */
static struct Scsi_Host *default_instance;

/*
 * Function : int sun3scsi_detect(Scsi_Host_Template * tpnt)
 *
 * Purpose : initializes mac NCR5380 driver based on the
 *	command line / compile time port and irq definitions.
 *
 * Inputs : tpnt - template for this SCSI adapter.
 *
 * Returns : 1 if a host adapter was found, 0 if not.
 *
 */
 
static int sun3scsi_detect(Scsi_Host_Template * tpnt)
{
	unsigned long ioaddr, irq = 0;
	static int called = 0;
	struct Scsi_Host *instance;
	int i;
	unsigned long addrs[3] = { IOBASE_SUN3_VMESCSI, 
				   IOBASE_SUN3_VMESCSI + 0x4000,
				   0 };
	unsigned long vecs[3] = { SUN3_VEC_VMESCSI0,
				  SUN3_VEC_VMESCSI1,
				  0 };
	/* check that this machine has an onboard 5380 */
	switch(idprom->id_machtype) {
	case SM_SUN3|SM_3_160:
	case SM_SUN3|SM_3_260:
		break;

	default:
		return 0;
	}

	if(called)
		return 0;

	tpnt->proc_name = "Sun3 5380 VME SCSI";

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
	
	ioaddr = 0;
	for(i = 0; addrs[i] != 0; i++) {
		unsigned char x;
		
		ioaddr = (unsigned long)sun3_ioremap(addrs[i], PAGE_SIZE,
						     SUN3_PAGE_TYPE_VME16);
		irq = vecs[i];
		sun3_scsi_regp = (unsigned char *)ioaddr;
		
		dregs = (struct sun3_dma_regs *)(((unsigned char *)ioaddr) + 8);
		
		if(sun3_map_test((unsigned long)dregs, &x)) {
			unsigned short oldcsr;

			oldcsr = dregs->csr;
			dregs->csr = 0;
			udelay(SUN3_DMA_DELAY);
			if(dregs->csr == 0x1400)
				break;
			
			dregs->csr = oldcsr;
		}

		iounmap((void *)ioaddr);
		ioaddr = 0;
	}

	if(!ioaddr)
		return 0;
	
#ifdef SUPPORT_TAGS
	if (setup_use_tagged_queuing < 0)
		setup_use_tagged_queuing = USE_TAGGED_QUEUING;
#endif

	instance = scsi_register (tpnt, sizeof(struct NCR5380_hostdata));
	if(instance == NULL)
		return 0;
		
	default_instance = instance;

        instance->io_port = (unsigned long) ioaddr;
	instance->irq = irq;

	NCR5380_init(instance, 0);

	instance->n_io_port = 32;

        ((struct NCR5380_hostdata *)instance->hostdata)->ctrl = 0;

	if (request_irq(instance->irq, scsi_sun3_intr,
			     0, "Sun3SCSI-5380VME", NULL)) {
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

	printk("scsi%d: Sun3 5380 VME at port %lX irq", instance->host_no, instance->io_port);
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
	dregs->fifo_count_hi = 0;
	dregs->dma_addr_hi = 0;
	dregs->dma_addr_lo = 0;
	dregs->dma_count_hi = 0;
	dregs->dma_count_lo = 0;

	dregs->ivect = VME_DATA24 | (instance->irq & 0xff);

	called = 1;

#ifdef RESET_BOOT
	sun3_scsi_reset_boot(instance);
#endif

	return 1;
}

int sun3scsi_release (struct Scsi_Host *shpnt)
{
	if (shpnt->irq != SCSI_IRQ_NONE)
		free_irq (shpnt->irq, NULL);

	iounmap((void *)sun3_scsi_regp);

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

static const char * sun3scsi_info (struct Scsi_Host *spnt) {
    return "";
}

// safe bits for the CSR
#define CSR_GOOD 0x060f

static irqreturn_t scsi_sun3_intr(int irq, void *dummy, struct pt_regs *fp)
{
	unsigned short csr = dregs->csr;
	int handled = 0;

	dregs->csr &= ~CSR_DMA_ENABLE;


#ifdef SUN3_SCSI_DEBUG
	printk("scsi_intr csr %x\n", csr);
#endif

	if(csr & ~CSR_GOOD) {
		if(csr & CSR_DMA_BUSERR) {
			printk("scsi%d: bus error in dma\n", default_instance->host_no);
#ifdef SUN3_SCSI_DEBUG
			printk("scsi: residual %x count %x addr %p dmaaddr %x\n", 
			       dregs->fifo_count,
			       dregs->dma_count_lo | (dregs->dma_count_hi << 16),
			       sun3_dma_orig_addr,
			       dregs->dma_addr_lo | (dregs->dma_addr_hi << 16));
#endif
		}

		if(csr & CSR_DMA_CONFLICT) {
			printk("scsi%d: dma conflict\n", default_instance->host_no);
		}
		handled = 1;
	}

	if(csr & (CSR_SDB_INT | CSR_DMA_INT)) {
		NCR5380_intr(irq, dummy, fp);
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
	void *addr;

	if(sun3_dma_orig_addr != NULL)
		dvma_unmap(sun3_dma_orig_addr);

//	addr = sun3_dvma_page((unsigned long)data, (unsigned long)dmabuf);
	addr = (void *)dvma_map_vme((unsigned long) data, count);
		
	sun3_dma_orig_addr = addr;
	sun3_dma_orig_count = count;
	
#ifdef SUN3_SCSI_DEBUG
	printk("scsi: dma_setup addr %p count %x\n", addr, count);
#endif

//	dregs->fifo_count = 0;
#if 0	
	/* reset fifo */
	dregs->csr &= ~CSR_FIFO;
	dregs->csr |= CSR_FIFO;
#endif	
	/* set direction */
	if(write_flag)
		dregs->csr |= CSR_SEND;
	else
		dregs->csr &= ~CSR_SEND;
	
	/* reset fifo */
//	dregs->csr &= ~CSR_FIFO;
//	dregs->csr |= CSR_FIFO;

	dregs->csr |= CSR_PACK_ENABLE;

	dregs->dma_addr_hi = ((unsigned long)addr >> 16);
	dregs->dma_addr_lo = ((unsigned long)addr & 0xffff);
	
	dregs->dma_count_hi = 0;
	dregs->dma_count_lo = 0;
	dregs->fifo_count_hi = 0;
	dregs->fifo_count = 0;
		
#ifdef SUN3_SCSI_DEBUG
	printk("scsi: dma_setup done csr %x\n", dregs->csr);
#endif
       	return count;

}

static inline unsigned long sun3scsi_dma_residual(struct Scsi_Host *instance)
{
	return last_residual;
}

static inline unsigned long sun3scsi_dma_xfer_len(unsigned long wanted, Scsi_Cmnd *cmd,
				    int write_flag)
{
	if(cmd->request->flags & REQ_CMD)
 		return wanted;
	else
		return 0;
}

static int sun3scsi_dma_start(unsigned long count, char *data)
{
	
	unsigned short csr;

	csr = dregs->csr;
#ifdef SUN3_SCSI_DEBUG
	printk("scsi: dma_start data %p count %x csr %x fifo %x\n", data, count, csr, dregs->fifo_count);
#endif
	
	dregs->dma_count_hi = (sun3_dma_orig_count >> 16);
	dregs->dma_count_lo = (sun3_dma_orig_count & 0xffff);

	dregs->fifo_count_hi = (sun3_dma_orig_count >> 16);
	dregs->fifo_count = (sun3_dma_orig_count & 0xffff);

//	if(!(csr & CSR_DMA_ENABLE))
//		dregs->csr |= CSR_DMA_ENABLE;

	return 0;
}

/* clean up after our dma is done */
static int sun3scsi_dma_finish(int write_flag)
{
	unsigned short fifo;
	int ret = 0;
	
	sun3_dma_active = 0;

	dregs->csr &= ~CSR_DMA_ENABLE;
	
	fifo = dregs->fifo_count;
	if(write_flag) {
		if((fifo > 0) && (fifo < sun3_dma_orig_count))
			fifo++;
	}

	last_residual = fifo;
#ifdef SUN3_SCSI_DEBUG
	printk("scsi: residual %x total %x\n", fifo, sun3_dma_orig_count);
#endif
	/* empty bytes from the fifo which didn't make it */
	if((!write_flag) && (dregs->csr & CSR_LEFT)) {
		unsigned char *vaddr;

#ifdef SUN3_SCSI_DEBUG
		printk("scsi: got left over bytes\n");
#endif

		vaddr = (unsigned char *)dvma_vmetov(sun3_dma_orig_addr);
		
		vaddr += (sun3_dma_orig_count - fifo);
		vaddr--;
		
		switch(dregs->csr & CSR_LEFT) {
		case CSR_LEFT_3:
			*vaddr = (dregs->bpack_lo & 0xff00) >> 8;
			vaddr--;
			
		case CSR_LEFT_2:
			*vaddr = (dregs->bpack_hi & 0x00ff);
			vaddr--;
			
		case CSR_LEFT_1:
			*vaddr = (dregs->bpack_hi & 0xff00) >> 8;
			break;
		}
		
		
	}

	dvma_unmap(sun3_dma_orig_addr);
	sun3_dma_orig_addr = NULL;

	dregs->dma_addr_hi = 0;
	dregs->dma_addr_lo = 0;
	dregs->dma_count_hi = 0;
	dregs->dma_count_lo = 0;

	dregs->fifo_count = 0;
	dregs->fifo_count_hi = 0;

	dregs->csr &= ~CSR_SEND;
	
//	dregs->csr |= CSR_DMA_ENABLE;
	
#if 0
	/* reset fifo */
	dregs->csr &= ~CSR_FIFO;
	dregs->csr |= CSR_FIFO;
#endif	
	sun3_dma_setup_done = NULL;

	return ret;

}

#include "sun3_NCR5380.c"

static Scsi_Host_Template driver_template = {
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

