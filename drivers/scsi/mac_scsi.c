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
 * $Log: mac_NCR5380.c,v $
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/ctype.h>
#include <linux/delay.h>

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_via.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "mac_scsi.h"

/* These control the behaviour of the generic 5380 core */
#define AUTOSENSE
#define PSEUDO_DMA

#include "NCR5380.h"

#if 0
#define NDEBUG (NDEBUG_INTR | NDEBUG_PSEUDO_DMA | NDEBUG_ARBITRATION | NDEBUG_SELECTION | NDEBUG_RESELECTION)
#else
#define NDEBUG (NDEBUG_ABORT)
#endif

#define RESET_BOOT
#define DRIVER_SETUP

extern void via_scsi_clear(void);

#ifdef RESET_BOOT
static void mac_scsi_reset_boot(struct Scsi_Host *instance);
#endif

static int setup_called = 0;
static int setup_can_queue = -1;
static int setup_cmd_per_lun = -1;
static int setup_sg_tablesize = -1;
static int setup_use_pdma = -1;
#ifdef SUPPORT_TAGS
static int setup_use_tagged_queuing = -1;
#endif
static int setup_hostid = -1;

/* Time (in jiffies) to wait after a reset; the SCSI standard calls for 250ms,
 * we usually do 0.5s to be on the safe side. But Toshiba CD-ROMs once more
 * need ten times the standard value... */
#define TOSHIBA_DELAY

#ifdef TOSHIBA_DELAY
#define	AFTER_RESET_DELAY	(5*HZ/2)
#else
#define	AFTER_RESET_DELAY	(HZ/2)
#endif

static volatile unsigned char *mac_scsi_regp = NULL;
static volatile unsigned char *mac_scsi_drq  = NULL;
static volatile unsigned char *mac_scsi_nodrq = NULL;


/*
 * NCR 5380 register access functions
 */

#if 0
/* Debug versions */
#define CTRL(p,v) (*ctrl = (v))

static char macscsi_read(struct Scsi_Host *instance, int reg)
{
  int iobase = instance->io_port;
  int i;
  int *ctrl = &((struct NCR5380_hostdata *)instance->hostdata)->ctrl;

  CTRL(iobase, 0);
  i = in_8(iobase + (reg<<4));
  CTRL(iobase, 0x40);

  return i;
}

static void macscsi_write(struct Scsi_Host *instance, int reg, int value)
{
  int iobase = instance->io_port;
  int *ctrl = &((struct NCR5380_hostdata *)instance->hostdata)->ctrl;

  CTRL(iobase, 0);
  out_8(iobase + (reg<<4), value);
  CTRL(iobase, 0x40);
}
#else

/* Fast versions */
static __inline__ char macscsi_read(struct Scsi_Host *instance, int reg)
{
  return in_8(instance->io_port + (reg<<4));
}

static __inline__ void macscsi_write(struct Scsi_Host *instance, int reg, int value)
{
  out_8(instance->io_port + (reg<<4), value);
}
#endif


/*
 * Function : mac_scsi_setup(char *str)
 *
 * Purpose : booter command line initialization of the overrides array,
 *
 * Inputs : str - comma delimited list of options
 *
 */

static int __init mac_scsi_setup(char *str) {
#ifdef DRIVER_SETUP	
	int ints[7];
	
	(void)get_options( str, ARRAY_SIZE(ints), ints);
	
	if (setup_called++ || ints[0] < 1 || ints[0] > 6) {
	    printk(KERN_WARNING "scsi: <mac5380>"
		" Usage: mac5380=<can_queue>[,<cmd_per_lun>,<sg_tablesize>,<hostid>,<use_tags>,<use_pdma>]\n");
	    printk(KERN_ALERT "scsi: <mac5380> Bad Penguin parameters?\n");
	    return 0;
	}
	    
	if (ints[0] >= 1) {
		if (ints[1] > 0)
			/* no limits on this, just > 0 */
			setup_can_queue = ints[1];
	}
	if (ints[0] >= 2) {
		if (ints[2] > 0)
			setup_cmd_per_lun = ints[2];
	}
	if (ints[0] >= 3) {
		if (ints[3] >= 0) {
			setup_sg_tablesize = ints[3];
			/* Must be <= SG_ALL (255) */
			if (setup_sg_tablesize > SG_ALL)
				setup_sg_tablesize = SG_ALL;
		}
	}
	if (ints[0] >= 4) {
		/* Must be between 0 and 7 */
		if (ints[4] >= 0 && ints[4] <= 7)
			setup_hostid = ints[4];
		else if (ints[4] > 7)
			printk(KERN_WARNING "mac_scsi_setup: invalid host ID %d !\n", ints[4] );
	}
#ifdef SUPPORT_TAGS	
	if (ints[0] >= 5) {
		if (ints[5] >= 0)
			setup_use_tagged_queuing = !!ints[5];
	}
	
	if (ints[0] == 6) {
	    if (ints[6] >= 0)
		setup_use_pdma = ints[6];
	}
#else
	if (ints[0] == 5) {
	    if (ints[5] >= 0)
		setup_use_pdma = ints[5];
	}
#endif /* SUPPORT_TAGS */
	
#endif /* DRIVER_SETUP */
	return 1;
}

__setup("mac5380=", mac_scsi_setup);

/*
 * Function : int macscsi_detect(struct scsi_host_template * tpnt)
 *
 * Purpose : initializes mac NCR5380 driver based on the
 *	command line / compile time port and irq definitions.
 *
 * Inputs : tpnt - template for this SCSI adapter.
 *
 * Returns : 1 if a host adapter was found, 0 if not.
 *
 */
 
int __init macscsi_detect(struct scsi_host_template * tpnt)
{
    static int called = 0;
    int flags = 0;
    struct Scsi_Host *instance;

    if (!MACH_IS_MAC || called)
	return( 0 );

    if (macintosh_config->scsi_type != MAC_SCSI_OLD)
	return( 0 );

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

#ifdef SUPPORT_TAGS
    if (setup_use_tagged_queuing < 0)
	setup_use_tagged_queuing = USE_TAGGED_QUEUING;
#endif

    /* Once we support multiple 5380s (e.g. DuoDock) we'll do
       something different here */
    instance = scsi_register (tpnt, sizeof(struct NCR5380_hostdata));

    if (macintosh_config->ident == MAC_MODEL_IIFX) {
	mac_scsi_regp  = via1+0x8000;
	mac_scsi_drq   = via1+0xE000;
	mac_scsi_nodrq = via1+0xC000;
	/* The IIFX should be able to do true DMA, but pseudo-dma doesn't work */
	flags = FLAG_NO_PSEUDO_DMA;
    } else {
	mac_scsi_regp  = via1+0x10000;
	mac_scsi_drq   = via1+0x6000;
	mac_scsi_nodrq = via1+0x12000;
    }

    if (! setup_use_pdma)
	flags = FLAG_NO_PSEUDO_DMA;
	
    instance->io_port = (unsigned long) mac_scsi_regp;
    instance->irq = IRQ_MAC_SCSI;

#ifdef RESET_BOOT   
    mac_scsi_reset_boot(instance);
#endif
    
    NCR5380_init(instance, flags);

    instance->n_io_port = 255;

    ((struct NCR5380_hostdata *)instance->hostdata)->ctrl = 0;

    if (instance->irq != SCSI_IRQ_NONE)
	if (request_irq(instance->irq, NCR5380_intr, 0, "ncr5380", instance)) {
	    printk(KERN_WARNING "scsi%d: IRQ%d not free, interrupts disabled\n",
		   instance->host_no, instance->irq);
	    instance->irq = SCSI_IRQ_NONE;
	}

    printk(KERN_INFO "scsi%d: generic 5380 at port %lX irq", instance->host_no, instance->io_port);
    if (instance->irq == SCSI_IRQ_NONE)
	printk (KERN_INFO "s disabled");
    else
	printk (KERN_INFO " %d", instance->irq);
    printk(KERN_INFO " options CAN_QUEUE=%d CMD_PER_LUN=%d release=%d",
	   instance->can_queue, instance->cmd_per_lun, MACSCSI_PUBLIC_RELEASE);
    printk(KERN_INFO "\nscsi%d:", instance->host_no);
    NCR5380_print_options(instance);
    printk("\n");
    called = 1;
    return 1;
}

int macscsi_release (struct Scsi_Host *shpnt)
{
	if (shpnt->irq != SCSI_IRQ_NONE)
		free_irq(shpnt->irq, shpnt);
	NCR5380_exit(shpnt);

	return 0;
}

#ifdef RESET_BOOT
/*
 * Our 'bus reset on boot' function
 */

static void mac_scsi_reset_boot(struct Scsi_Host *instance)
{
	unsigned long end;

	NCR5380_local_declare();
	NCR5380_setup(instance);
	
	/*
	 * Do a SCSI reset to clean up the bus during initialization. No messing
	 * with the queues, interrupts, or locks necessary here.
	 */

	printk(KERN_INFO "Macintosh SCSI: resetting the SCSI bus..." );

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

	printk(KERN_INFO " done\n" );
}
#endif

const char * macscsi_info (struct Scsi_Host *spnt) {
	return "";
}

/* 
   Pseudo-DMA: (Ove Edlund)
   The code attempts to catch bus errors that occur if one for example
   "trips over the cable".
   XXX: Since bus errors in the PDMA routines never happen on my 
   computer, the bus error code is untested. 
   If the code works as intended, a bus error results in Pseudo-DMA 
   beeing disabled, meaning that the driver switches to slow handshake. 
   If bus errors are NOT extremely rare, this has to be changed. 
*/

#define CP_IO_TO_MEM(s,d,len)				\
__asm__ __volatile__					\
    ("    cmp.w  #4,%2\n"				\
     "    bls    8f\n"					\
     "    move.w %1,%%d0\n"				\
     "    neg.b  %%d0\n"				\
     "    and.w  #3,%%d0\n"				\
     "    sub.w  %%d0,%2\n"				\
     "    bra    2f\n"					\
     " 1: move.b (%0),(%1)+\n"				\
     " 2: dbf    %%d0,1b\n"				\
     "    move.w %2,%%d0\n"				\
     "    lsr.w  #5,%%d0\n"				\
     "    bra    4f\n"					\
     " 3: move.l (%0),(%1)+\n"				\
     "31: move.l (%0),(%1)+\n"				\
     "32: move.l (%0),(%1)+\n"				\
     "33: move.l (%0),(%1)+\n"				\
     "34: move.l (%0),(%1)+\n"				\
     "35: move.l (%0),(%1)+\n"				\
     "36: move.l (%0),(%1)+\n"				\
     "37: move.l (%0),(%1)+\n"				\
     " 4: dbf    %%d0,3b\n"				\
     "    move.w %2,%%d0\n"				\
     "    lsr.w  #2,%%d0\n"				\
     "    and.w  #7,%%d0\n"				\
     "    bra    6f\n"					\
     " 5: move.l (%0),(%1)+\n"				\
     " 6: dbf    %%d0,5b\n"				\
     "    and.w  #3,%2\n"				\
     "    bra    8f\n"					\
     " 7: move.b (%0),(%1)+\n"				\
     " 8: dbf    %2,7b\n"				\
     "    moveq.l #0, %2\n"				\
     " 9: \n"						\
     ".section .fixup,\"ax\"\n"				\
     "    .even\n"					\
     "90: moveq.l #1, %2\n"				\
     "    jra 9b\n"					\
     ".previous\n"					\
     ".section __ex_table,\"a\"\n"			\
     "   .align 4\n"					\
     "   .long  1b,90b\n"				\
     "   .long  3b,90b\n"				\
     "   .long 31b,90b\n"				\
     "   .long 32b,90b\n"				\
     "   .long 33b,90b\n"				\
     "   .long 34b,90b\n"				\
     "   .long 35b,90b\n"				\
     "   .long 36b,90b\n"				\
     "   .long 37b,90b\n"				\
     "   .long  5b,90b\n"				\
     "   .long  7b,90b\n"				\
     ".previous"					\
     : "=a"(s), "=a"(d), "=d"(len)			\
     : "0"(s), "1"(d), "2"(len)				\
     : "d0")


static int macscsi_pread (struct Scsi_Host *instance,
			  unsigned char *dst, int len)
{
   unsigned char *d;
   volatile unsigned char *s;

   NCR5380_local_declare();
   NCR5380_setup(instance);

   s = mac_scsi_drq+0x60;
   d = dst;

/* These conditions are derived from MacOS */

   while (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ) 
         && !(NCR5380_read(STATUS_REG) & SR_REQ))
      ;
   if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ) 
         && (NCR5380_read(BUS_AND_STATUS_REG) & BASR_PHASE_MATCH)) {
      printk(KERN_ERR "Error in macscsi_pread\n");
      return -1;
   }

   CP_IO_TO_MEM(s, d, len);
   
   if (len != 0) {
      printk(KERN_NOTICE "Bus error in macscsi_pread\n");
      return -1;
   }
   
   return 0;
}


#define CP_MEM_TO_IO(s,d,len)				\
__asm__ __volatile__					\
    ("    cmp.w  #4,%2\n"				\
     "    bls    8f\n"					\
     "    move.w %0,%%d0\n"				\
     "    neg.b  %%d0\n"				\
     "    and.w  #3,%%d0\n"				\
     "    sub.w  %%d0,%2\n"				\
     "    bra    2f\n"					\
     " 1: move.b (%0)+,(%1)\n"				\
     " 2: dbf    %%d0,1b\n"				\
     "    move.w %2,%%d0\n"				\
     "    lsr.w  #5,%%d0\n"				\
     "    bra    4f\n"					\
     " 3: move.l (%0)+,(%1)\n"				\
     "31: move.l (%0)+,(%1)\n"				\
     "32: move.l (%0)+,(%1)\n"				\
     "33: move.l (%0)+,(%1)\n"				\
     "34: move.l (%0)+,(%1)\n"				\
     "35: move.l (%0)+,(%1)\n"				\
     "36: move.l (%0)+,(%1)\n"				\
     "37: move.l (%0)+,(%1)\n"				\
     " 4: dbf    %%d0,3b\n"				\
     "    move.w %2,%%d0\n"				\
     "    lsr.w  #2,%%d0\n"				\
     "    and.w  #7,%%d0\n"				\
     "    bra    6f\n"					\
     " 5: move.l (%0)+,(%1)\n"				\
     " 6: dbf    %%d0,5b\n"				\
     "    and.w  #3,%2\n"				\
     "    bra    8f\n"					\
     " 7: move.b (%0)+,(%1)\n"				\
     " 8: dbf    %2,7b\n"				\
     "    moveq.l #0, %2\n"				\
     " 9: \n"						\
     ".section .fixup,\"ax\"\n"				\
     "    .even\n"					\
     "90: moveq.l #1, %2\n"				\
     "    jra 9b\n"					\
     ".previous\n"					\
     ".section __ex_table,\"a\"\n"			\
     "   .align 4\n"					\
     "   .long  1b,90b\n"				\
     "   .long  3b,90b\n"				\
     "   .long 31b,90b\n"				\
     "   .long 32b,90b\n"				\
     "   .long 33b,90b\n"				\
     "   .long 34b,90b\n"				\
     "   .long 35b,90b\n"				\
     "   .long 36b,90b\n"				\
     "   .long 37b,90b\n"				\
     "   .long  5b,90b\n"				\
     "   .long  7b,90b\n"				\
     ".previous"					\
     : "=a"(s), "=a"(d), "=d"(len)			\
     : "0"(s), "1"(d), "2"(len)				\
     : "d0")

static int macscsi_pwrite (struct Scsi_Host *instance,
				  unsigned char *src, int len)
{
   unsigned char *s;
   volatile unsigned char *d;

   NCR5380_local_declare();
   NCR5380_setup(instance);

   s = src;
   d = mac_scsi_drq;
   
/* These conditions are derived from MacOS */

   while (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ) 
         && (!(NCR5380_read(STATUS_REG) & SR_REQ) 
            || (NCR5380_read(BUS_AND_STATUS_REG) & BASR_PHASE_MATCH))) 
      ;
   if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_DRQ)) {
      printk(KERN_ERR "Error in macscsi_pwrite\n");
      return -1;
   }

   CP_MEM_TO_IO(s, d, len);   

   if (len != 0) {
      printk(KERN_NOTICE "Bus error in macscsi_pwrite\n");
      return -1;
   }
   
   return 0;
}


#include "NCR5380.c"

static struct scsi_host_template driver_template = {
	.proc_name			= "Mac5380",
	.proc_info			= macscsi_proc_info,
	.name				= "Macintosh NCR5380 SCSI",
	.detect				= macscsi_detect,
	.release			= macscsi_release,
	.info				= macscsi_info,
	.queuecommand			= macscsi_queue_command,
	.eh_abort_handler		= macscsi_abort,
	.eh_bus_reset_handler		= macscsi_bus_reset,
	.can_queue			= CAN_QUEUE,
	.this_id			= 7,
	.sg_tablesize			= SG_ALL,
	.cmd_per_lun			= CMD_PER_LUN,
	.use_clustering			= DISABLE_CLUSTERING
};


#include "scsi_module.c"
