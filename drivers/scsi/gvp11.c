#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <linux/zorro.h>
#include <asm/irq.h>
#include <linux/spinlock.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "wd33c93.h"
#include "gvp11.h"

#include<linux/stat.h>

#define DMA(ptr) ((gvp11_scsiregs *)((ptr)->base))
#define HDATA(ptr) ((struct WD33C93_hostdata *)((ptr)->hostdata))

static irqreturn_t gvp11_intr (int irq, void *_instance, struct pt_regs *fp)
{
    unsigned long flags;
    unsigned int status;
    struct Scsi_Host *instance = (struct Scsi_Host *)_instance;

    status = DMA(instance)->CNTR;
    if (!(status & GVP11_DMAC_INT_PENDING))
	return IRQ_NONE;

    spin_lock_irqsave(instance->host_lock, flags);
    wd33c93_intr(instance);
    spin_unlock_irqrestore(instance->host_lock, flags);
    return IRQ_HANDLED;
}

static int gvp11_xfer_mask = 0;

void gvp11_setup (char *str, int *ints)
{
    gvp11_xfer_mask = ints[1];
}

static int dma_setup (Scsi_Cmnd *cmd, int dir_in)
{
    unsigned short cntr = GVP11_DMAC_INT_ENABLE;
    unsigned long addr = virt_to_bus(cmd->SCp.ptr);
    int bank_mask;
    static int scsi_alloc_out_of_range = 0;

    /* use bounce buffer if the physical address is bad */
    if (addr & HDATA(cmd->device->host)->dma_xfer_mask ||
	(!dir_in && mm_end_of_chunk (addr, cmd->SCp.this_residual)))
    {
	HDATA(cmd->device->host)->dma_bounce_len = (cmd->SCp.this_residual + 511)
	    & ~0x1ff;

 	if( !scsi_alloc_out_of_range ) {
	    HDATA(cmd->device->host)->dma_bounce_buffer =
		kmalloc (HDATA(cmd->device->host)->dma_bounce_len, GFP_KERNEL);
	    HDATA(cmd->device->host)->dma_buffer_pool = BUF_SCSI_ALLOCED;
	}

	if (scsi_alloc_out_of_range ||
	    !HDATA(cmd->device->host)->dma_bounce_buffer) {
	    HDATA(cmd->device->host)->dma_bounce_buffer =
		amiga_chip_alloc(HDATA(cmd->device->host)->dma_bounce_len,
				       "GVP II SCSI Bounce Buffer");

	    if(!HDATA(cmd->device->host)->dma_bounce_buffer)
	    {
		HDATA(cmd->device->host)->dma_bounce_len = 0;
		return 1;
	    }

	    HDATA(cmd->device->host)->dma_buffer_pool = BUF_CHIP_ALLOCED;
	}

	/* check if the address of the bounce buffer is OK */
	addr = virt_to_bus(HDATA(cmd->device->host)->dma_bounce_buffer);

	if (addr & HDATA(cmd->device->host)->dma_xfer_mask) {
	    /* fall back to Chip RAM if address out of range */
	    if( HDATA(cmd->device->host)->dma_buffer_pool == BUF_SCSI_ALLOCED) {
		kfree (HDATA(cmd->device->host)->dma_bounce_buffer);
		scsi_alloc_out_of_range = 1;
	    } else {
		amiga_chip_free (HDATA(cmd->device->host)->dma_bounce_buffer);
            }
		
	    HDATA(cmd->device->host)->dma_bounce_buffer =
		amiga_chip_alloc(HDATA(cmd->device->host)->dma_bounce_len,
				       "GVP II SCSI Bounce Buffer");

	    if(!HDATA(cmd->device->host)->dma_bounce_buffer)
	    {
		HDATA(cmd->device->host)->dma_bounce_len = 0;
		return 1;
	    }

	    addr = virt_to_bus(HDATA(cmd->device->host)->dma_bounce_buffer);
	    HDATA(cmd->device->host)->dma_buffer_pool = BUF_CHIP_ALLOCED;
	}
	    
	if (!dir_in) {
	    /* copy to bounce buffer for a write */
	    memcpy (HDATA(cmd->device->host)->dma_bounce_buffer,
		    cmd->SCp.ptr, cmd->SCp.this_residual);
	}
    }

    /* setup dma direction */
    if (!dir_in)
	cntr |= GVP11_DMAC_DIR_WRITE;

    HDATA(cmd->device->host)->dma_dir = dir_in;
    DMA(cmd->device->host)->CNTR = cntr;

    /* setup DMA *physical* address */
    DMA(cmd->device->host)->ACR = addr;

    if (dir_in)
	/* invalidate any cache */
	cache_clear (addr, cmd->SCp.this_residual);
    else
	/* push any dirty cache */
	cache_push (addr, cmd->SCp.this_residual);

    if ((bank_mask = (~HDATA(cmd->device->host)->dma_xfer_mask >> 18) & 0x01c0))
	    DMA(cmd->device->host)->BANK = bank_mask & (addr >> 18);

    /* start DMA */
    DMA(cmd->device->host)->ST_DMA = 1;

    /* return success */
    return 0;
}

static void dma_stop (struct Scsi_Host *instance, Scsi_Cmnd *SCpnt,
		      int status)
{
    /* stop DMA */
    DMA(instance)->SP_DMA = 1;
    /* remove write bit from CONTROL bits */
    DMA(instance)->CNTR = GVP11_DMAC_INT_ENABLE;

    /* copy from a bounce buffer, if necessary */
    if (status && HDATA(instance)->dma_bounce_buffer) {
	if (HDATA(instance)->dma_dir && SCpnt)
	    memcpy (SCpnt->SCp.ptr, 
		    HDATA(instance)->dma_bounce_buffer,
		    SCpnt->SCp.this_residual);
	
	if (HDATA(instance)->dma_buffer_pool == BUF_SCSI_ALLOCED)
	    kfree (HDATA(instance)->dma_bounce_buffer);
	else
	    amiga_chip_free(HDATA(instance)->dma_bounce_buffer);
	
	HDATA(instance)->dma_bounce_buffer = NULL;
	HDATA(instance)->dma_bounce_len = 0;
    }
}

#define CHECK_WD33C93

int __init gvp11_detect(struct scsi_host_template *tpnt)
{
    static unsigned char called = 0;
    struct Scsi_Host *instance;
    unsigned long address;
    unsigned int epc;
    struct zorro_dev *z = NULL;
    unsigned int default_dma_xfer_mask;
    wd33c93_regs regs;
    int num_gvp11 = 0;
#ifdef CHECK_WD33C93
    volatile unsigned char *sasr_3393, *scmd_3393;
    unsigned char save_sasr;
    unsigned char q, qq;
#endif

    if (!MACH_IS_AMIGA || called)
	return 0;
    called = 1;

    tpnt->proc_name = "GVP11";
    tpnt->proc_info = &wd33c93_proc_info;

    while ((z = zorro_find_device(ZORRO_WILDCARD, z))) {
	/* 
	 * This should (hopefully) be the correct way to identify
	 * all the different GVP SCSI controllers (except for the
	 * SERIES I though).
	 */

	if (z->id == ZORRO_PROD_GVP_COMBO_030_R3_SCSI ||
	    z->id == ZORRO_PROD_GVP_SERIES_II)
	    default_dma_xfer_mask = ~0x00ffffff;
	else if (z->id == ZORRO_PROD_GVP_GFORCE_030_SCSI ||
		 z->id == ZORRO_PROD_GVP_A530_SCSI ||
		 z->id == ZORRO_PROD_GVP_COMBO_030_R4_SCSI)
	    default_dma_xfer_mask = ~0x01ffffff;
	else if (z->id == ZORRO_PROD_GVP_A1291 ||
		 z->id == ZORRO_PROD_GVP_GFORCE_040_SCSI_1)
	    default_dma_xfer_mask = ~0x07ffffff;
	else
	    continue;

	/*
	 * Rumors state that some GVP ram boards use the same product
	 * code as the SCSI controllers. Therefore if the board-size
	 * is not 64KB we asume it is a ram board and bail out.
	 */
	if (z->resource.end-z->resource.start != 0xffff)
		continue;

	address = z->resource.start;
	if (!request_mem_region(address, 256, "wd33c93"))
	    continue;

#ifdef CHECK_WD33C93

	/*
	 * These darn GVP boards are a problem - it can be tough to tell
	 * whether or not they include a SCSI controller. This is the
	 * ultimate Yet-Another-GVP-Detection-Hack in that it actually
	 * probes for a WD33c93 chip: If we find one, it's extremely
	 * likely that this card supports SCSI, regardless of Product_
	 * Code, Board_Size, etc. 
	 */

    /* Get pointers to the presumed register locations and save contents */

	sasr_3393 = &(((gvp11_scsiregs *)(ZTWO_VADDR(address)))->SASR);
	scmd_3393 = &(((gvp11_scsiregs *)(ZTWO_VADDR(address)))->SCMD);
	save_sasr = *sasr_3393;

    /* First test the AuxStatus Reg */

	q = *sasr_3393;		/* read it */
	if (q & 0x08)		/* bit 3 should always be clear */
		goto release;
	*sasr_3393 = WD_AUXILIARY_STATUS;	 /* setup indirect address */
	if (*sasr_3393 == WD_AUXILIARY_STATUS) { /* shouldn't retain the write */
		*sasr_3393 = save_sasr;	/* Oops - restore this byte */
		goto release;
		}
	if (*sasr_3393 != q) {	/* should still read the same */
		*sasr_3393 = save_sasr;	/* Oops - restore this byte */
		goto release;
		}
	if (*scmd_3393 != q)	/* and so should the image at 0x1f */
		goto release;


    /* Ok, we probably have a wd33c93, but let's check a few other places
     * for good measure. Make sure that this works for both 'A and 'B    
     * chip versions.
     */

	*sasr_3393 = WD_SCSI_STATUS;
	q = *scmd_3393;
	*sasr_3393 = WD_SCSI_STATUS;
	*scmd_3393 = ~q;
	*sasr_3393 = WD_SCSI_STATUS;
	qq = *scmd_3393;
	*sasr_3393 = WD_SCSI_STATUS;
	*scmd_3393 = q;
	if (qq != q)			/* should be read only */
		goto release;
	*sasr_3393 = 0x1e;	/* this register is unimplemented */
	q = *scmd_3393;
	*sasr_3393 = 0x1e;
	*scmd_3393 = ~q;
	*sasr_3393 = 0x1e;
	qq = *scmd_3393;
	*sasr_3393 = 0x1e;
	*scmd_3393 = q;
	if (qq != q || qq != 0xff)	/* should be read only, all 1's */
		goto release;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	q = *scmd_3393;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	*scmd_3393 = ~q;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	qq = *scmd_3393;
	*sasr_3393 = WD_TIMEOUT_PERIOD;
	*scmd_3393 = q;
	if (qq != (~q & 0xff))		/* should be read/write */
		goto release;
#endif

	instance = scsi_register (tpnt, sizeof (struct WD33C93_hostdata));
	if(instance == NULL)
		goto release;
	instance->base = ZTWO_VADDR(address);
	instance->irq = IRQ_AMIGA_PORTS;
	instance->unique_id = z->slotaddr;

	if (gvp11_xfer_mask)
		HDATA(instance)->dma_xfer_mask = gvp11_xfer_mask;
	else
		HDATA(instance)->dma_xfer_mask = default_dma_xfer_mask;


	DMA(instance)->secret2 = 1;
	DMA(instance)->secret1 = 0;
	DMA(instance)->secret3 = 15;
	while (DMA(instance)->CNTR & GVP11_DMAC_BUSY) ;
	DMA(instance)->CNTR = 0;

	DMA(instance)->BANK = 0;

	epc = *(unsigned short *)(ZTWO_VADDR(address) + 0x8000);

	/*
	 * Check for 14MHz SCSI clock
	 */
	regs.SASR = &(DMA(instance)->SASR);
	regs.SCMD = &(DMA(instance)->SCMD);
	wd33c93_init(instance, regs, dma_setup, dma_stop,
		     (epc & GVP_SCSICLKMASK) ? WD33C93_FS_8_10
					     : WD33C93_FS_12_15);

	request_irq(IRQ_AMIGA_PORTS, gvp11_intr, SA_SHIRQ, "GVP11 SCSI",
		    instance);
	DMA(instance)->CNTR = GVP11_DMAC_INT_ENABLE;
	num_gvp11++;
	continue;

release:
	release_mem_region(address, 256);
    }

    return num_gvp11;
}

static int gvp11_bus_reset(Scsi_Cmnd *cmd)
{
	/* FIXME perform bus-specific reset */

	/* FIXME 2: shouldn't we no-op this function (return
	   FAILED), and fall back to host reset function,
	   wd33c93_host_reset ? */

	spin_lock_irq(cmd->device->host->host_lock);
	wd33c93_host_reset(cmd);
	spin_unlock_irq(cmd->device->host->host_lock);

	return SUCCESS;
}


#define HOSTS_C

#include "gvp11.h"

static struct scsi_host_template driver_template = {
	.proc_name		= "GVP11",
	.name			= "GVP Series II SCSI",
	.detect			= gvp11_detect,
	.release		= gvp11_release,
	.queuecommand		= wd33c93_queuecommand,
	.eh_abort_handler	= wd33c93_abort,
	.eh_bus_reset_handler	= gvp11_bus_reset,
	.eh_host_reset_handler	= wd33c93_host_reset,
	.can_queue		= CAN_QUEUE,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= CMD_PER_LUN,
	.use_clustering		= DISABLE_CLUSTERING
};


#include "scsi_module.c"

int gvp11_release(struct Scsi_Host *instance)
{
#ifdef MODULE
    DMA(instance)->CNTR = 0;
    release_mem_region(ZTWO_PADDR(instance->base), 256);
    free_irq(IRQ_AMIGA_PORTS, instance);
    wd33c93_release();
#endif
    return 1;
}

MODULE_LICENSE("GPL");
