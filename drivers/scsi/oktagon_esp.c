/*
 * Oktagon_esp.c -- Driver for bsc Oktagon
 *
 * Written by Carsten Pluntke 1998
 *
 * Based on cyber_esp.c
 */

#include <linux/config.h>

#if defined(CONFIG_AMIGA) || defined(CONFIG_APUS)
#define USE_BOTTOM_HALF
#endif

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/reboot.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/pgtable.h>


#include "scsi.h"
#include <scsi/scsi_host.h>
#include "NCR53C9x.h"

#include <linux/zorro.h>
#include <asm/irq.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>

#ifdef USE_BOTTOM_HALF
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#endif

/* The controller registers can be found in the Z2 config area at these
 * offsets:
 */
#define OKTAGON_ESP_ADDR 0x03000
#define OKTAGON_DMA_ADDR 0x01000


static int  dma_bytes_sent(struct NCR_ESP *esp, int fifo_count);
static int  dma_can_transfer(struct NCR_ESP *esp, Scsi_Cmnd *sp);
static void dma_dump_state(struct NCR_ESP *esp);
static void dma_init_read(struct NCR_ESP *esp, __u32 vaddress, int length);
static void dma_init_write(struct NCR_ESP *esp, __u32 vaddress, int length);
static void dma_ints_off(struct NCR_ESP *esp);
static void dma_ints_on(struct NCR_ESP *esp);
static int  dma_irq_p(struct NCR_ESP *esp);
static void dma_led_off(struct NCR_ESP *esp);
static void dma_led_on(struct NCR_ESP *esp);
static int  dma_ports_p(struct NCR_ESP *esp);
static void dma_setup(struct NCR_ESP *esp, __u32 addr, int count, int write);

static void dma_irq_exit(struct NCR_ESP *esp);
static void dma_invalidate(struct NCR_ESP *esp);

static void dma_mmu_get_scsi_one(struct NCR_ESP *,Scsi_Cmnd *);
static void dma_mmu_get_scsi_sgl(struct NCR_ESP *,Scsi_Cmnd *);
static void dma_mmu_release_scsi_one(struct NCR_ESP *,Scsi_Cmnd *);
static void dma_mmu_release_scsi_sgl(struct NCR_ESP *,Scsi_Cmnd *);
static void dma_advance_sg(Scsi_Cmnd *);
static int  oktagon_notify_reboot(struct notifier_block *this, unsigned long code, void *x);

#ifdef USE_BOTTOM_HALF
static void dma_commit(void *opaque);

long oktag_to_io(long *paddr, long *addr, long len);
long oktag_from_io(long *addr, long *paddr, long len);

static DECLARE_WORK(tq_fake_dma, dma_commit, NULL);

#define DMA_MAXTRANSFER 0x8000

#else

/*
 * No bottom half. Use transfer directly from IRQ. Find a narrow path
 * between too much IRQ overhead and clogging the IRQ for too long.
 */

#define DMA_MAXTRANSFER 0x1000

#endif

static struct notifier_block oktagon_notifier = { 
	oktagon_notify_reboot,
	NULL,
	0
};

static long *paddress;
static long *address;
static long len;
static long dma_on;
static int direction;
static struct NCR_ESP *current_esp;


static volatile unsigned char cmd_buffer[16];
				/* This is where all commands are put
				 * before they are trasfered to the ESP chip
				 * via PIO.
				 */

/***************************************************************** Detection */
int oktagon_esp_detect(Scsi_Host_Template *tpnt)
{
	struct NCR_ESP *esp;
	struct zorro_dev *z = NULL;
	unsigned long address;
	struct ESP_regs *eregs;

	while ((z = zorro_find_device(ZORRO_PROD_BSC_OKTAGON_2008, z))) {
	    unsigned long board = z->resource.start;
	    if (request_mem_region(board+OKTAGON_ESP_ADDR,
				   sizeof(struct ESP_regs), "NCR53C9x")) {
		/*
		 * It is a SCSI controller.
		 * Hardwire Host adapter to SCSI ID 7
		 */
		
		address = (unsigned long)ZTWO_VADDR(board);
		eregs = (struct ESP_regs *)(address + OKTAGON_ESP_ADDR);

		/* This line was 5 lines lower */
		esp = esp_allocate(tpnt, (void *)board+OKTAGON_ESP_ADDR);

		/* we have to shift the registers only one bit for oktagon */
		esp->shift = 1;

		esp_write(eregs->esp_cfg1, (ESP_CONFIG1_PENABLE | 7));
		udelay(5);
		if (esp_read(eregs->esp_cfg1) != (ESP_CONFIG1_PENABLE | 7))
			return 0; /* Bail out if address did not hold data */

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
		esp->dma_invalidate = &dma_invalidate;
		esp->dma_irq_entry = 0;
		esp->dma_irq_exit = &dma_irq_exit;
		esp->dma_led_on = &dma_led_on;
		esp->dma_led_off = &dma_led_off;
		esp->dma_poll = 0;
		esp->dma_reset = 0;

		esp->dma_mmu_get_scsi_one = &dma_mmu_get_scsi_one;
		esp->dma_mmu_get_scsi_sgl = &dma_mmu_get_scsi_sgl;
		esp->dma_mmu_release_scsi_one = &dma_mmu_release_scsi_one;
		esp->dma_mmu_release_scsi_sgl = &dma_mmu_release_scsi_sgl;
		esp->dma_advance_sg = &dma_advance_sg;

		/* SCSI chip speed */
		/* Looking at the quartz of the SCSI board... */
		esp->cfreq = 25000000;

		/* The DMA registers on the CyberStorm are mapped
		 * relative to the device (i.e. in the same Zorro
		 * I/O block).
		 */
		esp->dregs = (void *)(address + OKTAGON_DMA_ADDR);

		paddress = (long *) esp->dregs;

		/* ESP register base */
		esp->eregs = eregs;
		
		/* Set the command buffer */
		esp->esp_command = (volatile unsigned char*) cmd_buffer;

		/* Yes, the virtual address. See below. */
		esp->esp_command_dvma = (__u32) cmd_buffer;

		esp->irq = IRQ_AMIGA_PORTS;
		request_irq(IRQ_AMIGA_PORTS, esp_intr, SA_SHIRQ,
			    "BSC Oktagon SCSI", esp->ehost);

		/* Figure out our scsi ID on the bus */
		esp->scsi_id = 7;
		
		/* We don't have a differential SCSI-bus. */
		esp->diff = 0;

		esp_initialize(esp);

		printk("ESP_Oktagon Driver 1.1"
#ifdef USE_BOTTOM_HALF
		       " [BOTTOM_HALF]"
#else
		       " [IRQ]"
#endif
		       " registered.\n");
		printk("ESP: Total of %d ESP hosts found, %d actually in use.\n", nesps,esps_in_use);
		esps_running = esps_in_use;
		current_esp = esp;
		register_reboot_notifier(&oktagon_notifier);
		return esps_in_use;
	    }
	}
	return 0;
}


/*
 * On certain configurations the SCSI equipment gets confused on reboot,
 * so we have to reset it then.
 */

static int
oktagon_notify_reboot(struct notifier_block *this, unsigned long code, void *x)
{
  struct NCR_ESP *esp;
  
  if((code == SYS_DOWN || code == SYS_HALT) && (esp = current_esp))
   {
    esp_bootup_reset(esp,esp->eregs);
    udelay(500); /* Settle time. Maybe unnecessary. */
   }
  return NOTIFY_DONE;
}
    

	
#ifdef USE_BOTTOM_HALF


/*
 * The bsc Oktagon controller has no real DMA, so we have to do the 'DMA
 * transfer' in the interrupt (Yikes!) or use a bottom half to not to clutter
 * IRQ's for longer-than-good.
 *
 * FIXME
 * BIG PROBLEM: 'len' is usually the buffer length, not the expected length
 * of the data. So DMA may finish prematurely, further reads lead to
 * 'machine check' on APUS systems (don't know about m68k systems, AmigaOS
 * deliberately ignores the bus faults) and a normal copy-loop can't
 * be exited prematurely just at the right moment by the dma_invalidate IRQ.
 * So do it the hard way, write an own copier in assembler and
 * catch the exception.
 *                                     -- Carsten
 */
 
 
static void dma_commit(void *opaque)
{
    long wait,len2,pos;
    struct NCR_ESP *esp;

    ESPDATA(("Transfer: %ld bytes, Address 0x%08lX, Direction: %d\n",
         len,(long) address,direction));
    dma_ints_off(current_esp);

    pos = 0;
    wait = 1;
    if(direction) /* write? (memory to device) */
     {
      while(len > 0)
       {
        len2 = oktag_to_io(paddress, address+pos, len);
	if(!len2)
	 {
	  if(wait > 1000)
	   {
	    printk("Expedited DMA exit (writing) %ld\n",len);
	    break;
	   }
	  mdelay(wait);
	  wait *= 2;
	 }
	pos += len2;
	len -= len2*sizeof(long);
       }
     } else {
      while(len > 0)
       {
        len2 = oktag_from_io(address+pos, paddress, len);
	if(!len2)
	 {
	  if(wait > 1000)
	   {
	    printk("Expedited DMA exit (reading) %ld\n",len);
	    break;
	   }
	  mdelay(wait);
	  wait *= 2;
	 }
	pos += len2;
	len -= len2*sizeof(long);
       }
     }

    /* to make esp->shift work */
    esp=current_esp;

#if 0
    len2 = (esp_read(current_esp->eregs->esp_tclow) & 0xff) |
           ((esp_read(current_esp->eregs->esp_tcmed) & 0xff) << 8);

    /*
     * Uh uh. If you see this, len and transfer count registers were out of
     * sync. That means really serious trouble.
     */

    if(len2)
      printk("Eeeek!! Transfer count still %ld!\n",len2);
#endif

    /*
     * Normally we just need to exit and wait for the interrupt to come.
     * But at least one device (my Microtek ScanMaker 630) regularly mis-
     * calculates the bytes it should send which is really ugly because
     * it locks up the SCSI bus if not accounted for.
     */

    if(!(esp_read(current_esp->eregs->esp_status) & ESP_STAT_INTR))
     {
      long len = 100;
      long trash[10];

      /*
       * Interrupt bit was not set. Either the device is just plain lazy
       * so we give it a 10 ms chance or...
       */
      while(len-- && (!(esp_read(current_esp->eregs->esp_status) & ESP_STAT_INTR)))
        udelay(100);


      if(!(esp_read(current_esp->eregs->esp_status) & ESP_STAT_INTR))
       {
        /*
	 * So we think that the transfer count is out of sync. Since we
	 * have all we want we are happy and can ditch the trash.
	 */
	 
        len = DMA_MAXTRANSFER;

        while(len-- && (!(esp_read(current_esp->eregs->esp_status) & ESP_STAT_INTR)))
          oktag_from_io(trash,paddress,2);

        if(!(esp_read(current_esp->eregs->esp_status) & ESP_STAT_INTR))
         {
          /*
           * Things really have gone wrong. If we leave the system in that
           * state, the SCSI bus is locked forever. I hope that this will
           * turn the system in a more or less running state.
           */
          printk("Device is bolixed, trying bus reset...\n");
	  esp_bootup_reset(current_esp,current_esp->eregs);
         }
       }
     }

    ESPDATA(("Transfer_finale: do_data_finale should come\n"));

    len = 0;
    dma_on = 0;
    dma_ints_on(current_esp);
}

#endif

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
	unsigned long sz = sp->SCp.this_residual;
	if(sz > DMA_MAXTRANSFER)
		sz = DMA_MAXTRANSFER;
	return sz;
}

static void dma_dump_state(struct NCR_ESP *esp)
{
}

/*
 * What the f$@& is this?
 *
 * Some SCSI devices (like my Microtek ScanMaker 630 scanner) want to transfer
 * more data than requested. How much? Dunno. So ditch the bogus data into
 * the sink, hoping the device will advance to the next phase sooner or later.
 *
 *                         -- Carsten
 */

static long oktag_eva_buffer[16]; /* The data sink */

static void oktag_check_dma(void)
{
  struct NCR_ESP *esp;

  esp=current_esp;
  if(!len)
   {
    address = oktag_eva_buffer;
    len = 2;
    /* esp_do_data sets them to zero like len */
    esp_write(current_esp->eregs->esp_tclow,2);
    esp_write(current_esp->eregs->esp_tcmed,0);
   }
}

static void dma_init_read(struct NCR_ESP *esp, __u32 vaddress, int length)
{
	/* Zorro is noncached, everything else done using processor. */
	/* cache_clear(addr, length); */
	
	if(dma_on)
	  panic("dma_init_read while dma process is initialized/running!\n");
	direction = 0;
	address = (long *) vaddress;
	current_esp = esp;
	len = length;
	oktag_check_dma();
        dma_on = 1;
}

static void dma_init_write(struct NCR_ESP *esp, __u32 vaddress, int length)
{
	/* cache_push(addr, length); */

	if(dma_on)
	  panic("dma_init_write while dma process is initialized/running!\n");
	direction = 1;
	address = (long *) vaddress;
	current_esp = esp;
	len = length;
	oktag_check_dma();
	dma_on = 1;
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
}

static void dma_led_on(struct NCR_ESP *esp)
{
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

/*
 * IRQ entry when DMA transfer is ready to be started
 */

static void dma_irq_exit(struct NCR_ESP *esp)
{
#ifdef USE_BOTTOM_HALF
	if(dma_on)
	 {
	  schedule_work(&tq_fake_dma);
	 }
#else
	while(len && !dma_irq_p(esp))
	 {
	  if(direction)
	    *paddress = *address++;
	   else
	    *address++ = *paddress;
	  len -= (sizeof(long));
	 }
	len = 0;
        dma_on = 0;
#endif
}

/*
 * IRQ entry when DMA has just finished
 */

static void dma_invalidate(struct NCR_ESP *esp)
{
}

/*
 * Since the processor does the data transfer we have to use the custom
 * mmu interface to pass the virtual address, not the physical.
 */

void dma_mmu_get_scsi_one(struct NCR_ESP *esp, Scsi_Cmnd *sp)
{
        sp->SCp.ptr =
                sp->request_buffer;
}

void dma_mmu_get_scsi_sgl(struct NCR_ESP *esp, Scsi_Cmnd *sp)
{
        sp->SCp.ptr = page_address(sp->SCp.buffer->page)+
		      sp->SCp.buffer->offset;
}

void dma_mmu_release_scsi_one(struct NCR_ESP *esp, Scsi_Cmnd *sp)
{
}

void dma_mmu_release_scsi_sgl(struct NCR_ESP *esp, Scsi_Cmnd *sp)
{
}

void dma_advance_sg(Scsi_Cmnd *sp)
{
	sp->SCp.ptr = page_address(sp->SCp.buffer->page)+
		      sp->SCp.buffer->offset;
}


#define HOSTS_C

int oktagon_esp_release(struct Scsi_Host *instance)
{
#ifdef MODULE
	unsigned long address = (unsigned long)((struct NCR_ESP *)instance->hostdata)->edev;
	esp_release();
	release_mem_region(address, sizeof(struct ESP_regs));
	free_irq(IRQ_AMIGA_PORTS, esp_intr);
	unregister_reboot_notifier(&oktagon_notifier);
#endif
	return 1;
}


static Scsi_Host_Template driver_template = {
	.proc_name		= "esp-oktagon",
	.proc_info		= &esp_proc_info,
	.name			= "BSC Oktagon SCSI",
	.detect			= oktagon_esp_detect,
	.slave_alloc		= esp_slave_alloc,
	.slave_destroy		= esp_slave_destroy,
	.release		= oktagon_esp_release,
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
