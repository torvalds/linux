/*  $Id$
 *  1993/03/31
 *  linux/kernel/aha1740.c
 *
 *  Based loosely on aha1542.c which is
 *  Copyright (C) 1992  Tommy Thorn and
 *  Modified by Eric Youngdale
 *
 *  This file is aha1740.c, written and
 *  Copyright (C) 1992,1993  Brad McLean
 *  brad@saturn.gaylord.com or brad@bradpc.gaylord.com.
 *  
 *  Modifications to makecode and queuecommand
 *  for proper handling of multiple devices courteously
 *  provided by Michael Weller, March, 1993
 *
 *  Multiple adapter support, extended translation detection,
 *  update to current scsi subsystem changes, proc fs support,
 *  working (!) module support based on patches from Andreas Arens,
 *  by Andreas Degert <ad@papyrus.hamburg.com>, 2/1997
 *
 * aha1740_makecode may still need even more work
 * if it doesn't work for your devices, take a look.
 *
 * Reworked for new_eh and new locking by Alan Cox <alan@redhat.com>
 *
 * Converted to EISA and generic DMA APIs by Marc Zyngier
 * <maz@wild-wind.fr.eu.org>, 4/2003.
 *
 * Shared interrupt support added by Rask Ingemann Lambertsen
 * <rask@sygehus.dk>, 10/2003
 *
 * For the avoidance of doubt the "preferred form" of this code is one which
 * is in an open non patent encumbered format. Where cryptographic key signing
 * forms part of the process of creating an executable the information
 * including keys needed to generate an equivalently functional executable
 * are deemed to be part of the source code.
 */

#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/eisa.h>
#include <linux/dma-mapping.h>

#include <asm/dma.h>
#include <asm/system.h>
#include <asm/io.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "aha1740.h"

/* IF YOU ARE HAVING PROBLEMS WITH THIS DRIVER, AND WANT TO WATCH
   IT WORK, THEN:
#define DEBUG
*/
#ifdef DEBUG
#define DEB(x) x
#else
#define DEB(x)
#endif

struct aha1740_hostdata {
	struct eisa_device *edev;
	unsigned int translation;
	unsigned int last_ecb_used;
	dma_addr_t ecb_dma_addr;
	struct ecb ecb[AHA1740_ECBS];
};

struct aha1740_sg {
	struct aha1740_chain sg_chain[AHA1740_SCATTER];
	dma_addr_t sg_dma_addr;
	dma_addr_t buf_dma_addr;
};

#define HOSTDATA(host) ((struct aha1740_hostdata *) &host->hostdata)

static inline struct ecb *ecb_dma_to_cpu (struct Scsi_Host *host,
					  dma_addr_t dma)
{
	struct aha1740_hostdata *hdata = HOSTDATA (host);
	dma_addr_t offset;

	offset = dma - hdata->ecb_dma_addr;

	return (struct ecb *)(((char *) hdata->ecb) + (unsigned int) offset);
}

static inline dma_addr_t ecb_cpu_to_dma (struct Scsi_Host *host, void *cpu)
{
	struct aha1740_hostdata *hdata = HOSTDATA (host);
	dma_addr_t offset;
    
	offset = (char *) cpu - (char *) hdata->ecb;

	return hdata->ecb_dma_addr + offset;
}

static int aha1740_proc_info(struct Scsi_Host *shpnt, char *buffer,
			     char **start, off_t offset,
			     int length, int inout)
{
	int len;
	struct aha1740_hostdata *host;

	if (inout)
		return-ENOSYS;

	host = HOSTDATA(shpnt);

	len = sprintf(buffer, "aha174x at IO:%lx, IRQ %d, SLOT %d.\n"
		      "Extended translation %sabled.\n",
		      shpnt->io_port, shpnt->irq, host->edev->slot,
		      host->translation ? "en" : "dis");

	if (offset > len) {
		*start = buffer;
		return 0;
	}

	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	return len;
}

static int aha1740_makecode(unchar *sense, unchar *status)
{
	struct statusword
	{
		ushort	don:1,	/* Command Done - No Error */
			du:1,	/* Data underrun */
		    :1,	qf:1,	/* Queue full */
		        sc:1,	/* Specification Check */
		        dor:1,	/* Data overrun */
		        ch:1,	/* Chaining Halted */
		        intr:1,	/* Interrupt issued */
		        asa:1,	/* Additional Status Available */
		        sns:1,	/* Sense information Stored */
		    :1,	ini:1,	/* Initialization Required */
			me:1,	/* Major error or exception */
		    :1,	eca:1,  /* Extended Contingent alliance */
		    :1;
	} status_word;
	int retval = DID_OK;

	status_word = * (struct statusword *) status;
#ifdef DEBUG
	printk("makecode from %x,%x,%x,%x %x,%x,%x,%x",
	       status[0], status[1], status[2], status[3],
	       sense[0], sense[1], sense[2], sense[3]);
#endif
	if (!status_word.don) { /* Anything abnormal was detected */
		if ( (status[1]&0x18) || status_word.sc ) {
			/*Additional info available*/
			/* Use the supplied info for further diagnostics */
			switch ( status[2] ) {
			case 0x12:
				if ( status_word.dor )
					retval=DID_ERROR; /* It's an Overrun */
				/* If not overrun, assume underrun and
				 * ignore it! */
			case 0x00: /* No info, assume no error, should
				    * not occur */
				break;
			case 0x11:
			case 0x21:
				retval=DID_TIME_OUT;
				break;
			case 0x0a:
				retval=DID_BAD_TARGET;
				break;
			case 0x04:
			case 0x05:
				retval=DID_ABORT;
				/* Either by this driver or the
				 * AHA1740 itself */
				break;
			default:
				retval=DID_ERROR; /* No further
						   * diagnostics
						   * possible */
			}
		} else {
			/* Michael suggests, and Brad concurs: */
			if ( status_word.qf ) {
				retval = DID_TIME_OUT; /* forces a redo */
				/* I think this specific one should
				 * not happen -Brad */
				printk("aha1740.c: WARNING: AHA1740 queue overflow!\n");
			} else
				if ( status[0]&0x60 ) {
					 /* Didn't find a better error */
					retval = DID_ERROR;
				}
			/* In any other case return DID_OK so for example
			   CONDITION_CHECKS make it through to the appropriate
			   device driver */
		}
	}
	/* Under all circumstances supply the target status -Michael */
	return status[3] | retval << 16;
}

static int aha1740_test_port(unsigned int base)
{
	if ( inb(PORTADR(base)) & PORTADDR_ENH )
		return 1;   /* Okay, we're all set */
	
	printk("aha174x: Board detected, but not in enhanced mode, so disabled it.\n");
	return 0;
}

/* A "high" level interrupt handler */
static irqreturn_t aha1740_intr_handle(int irq, void *dev_id,
				       struct pt_regs *regs)
{
	struct Scsi_Host *host = (struct Scsi_Host *) dev_id;
        void (*my_done)(Scsi_Cmnd *);
	int errstatus, adapstat;
	int number_serviced;
	struct ecb *ecbptr;
	Scsi_Cmnd *SCtmp;
	unsigned int base;
	unsigned long flags;
	int handled = 0;
	struct aha1740_sg *sgptr;
	struct eisa_device *edev;
	
	if (!host)
		panic("aha1740.c: Irq from unknown host!\n");
	spin_lock_irqsave(host->host_lock, flags);
	base = host->io_port;
	number_serviced = 0;
	edev = HOSTDATA(host)->edev;

	while(inb(G2STAT(base)) & G2STAT_INTPEND) {
		handled = 1;
		DEB(printk("aha1740_intr top of loop.\n"));
		adapstat = inb(G2INTST(base));
		ecbptr = ecb_dma_to_cpu (host, inl(MBOXIN0(base)));
		outb(G2CNTRL_IRST,G2CNTRL(base)); /* interrupt reset */
      
		switch ( adapstat & G2INTST_MASK ) {
		case	G2INTST_CCBRETRY:
		case	G2INTST_CCBERROR:
		case	G2INTST_CCBGOOD:
			/* Host Ready -> Mailbox in complete */
			outb(G2CNTRL_HRDY,G2CNTRL(base));
			if (!ecbptr) {
				printk("Aha1740 null ecbptr in interrupt (%x,%x,%x,%d)\n",
				       inb(G2STAT(base)),adapstat,
				       inb(G2INTST(base)), number_serviced++);
				continue;
			}
			SCtmp = ecbptr->SCpnt;
			if (!SCtmp) {
				printk("Aha1740 null SCtmp in interrupt (%x,%x,%x,%d)\n",
				       inb(G2STAT(base)),adapstat,
				       inb(G2INTST(base)), number_serviced++);
				continue;
			}
			sgptr = (struct aha1740_sg *) SCtmp->host_scribble;
			if (SCtmp->use_sg) {
				/* We used scatter-gather.
				   Do the unmapping dance. */
				dma_unmap_sg (&edev->dev,
					      (struct scatterlist *) SCtmp->request_buffer,
					      SCtmp->use_sg,
					      SCtmp->sc_data_direction);
			} else {
				dma_unmap_single (&edev->dev,
						  sgptr->buf_dma_addr,
						  SCtmp->request_bufflen,
						  DMA_BIDIRECTIONAL);
			}
	    
			/* Free the sg block */
			dma_free_coherent (&edev->dev,
					   sizeof (struct aha1740_sg),
					   SCtmp->host_scribble,
					   sgptr->sg_dma_addr);
	    
			/* Fetch the sense data, and tuck it away, in
			   the required slot.  The Adaptec
			   automatically fetches it, and there is no
			   guarantee that we will still have it in the
			   cdb when we come back */
			if ( (adapstat & G2INTST_MASK) == G2INTST_CCBERROR ) {
				memcpy(SCtmp->sense_buffer, ecbptr->sense, 
				       sizeof(SCtmp->sense_buffer));
				errstatus = aha1740_makecode(ecbptr->sense,ecbptr->status);
			} else
				errstatus = 0;
			DEB(if (errstatus)
			    printk("aha1740_intr_handle: returning %6x\n",
				   errstatus));
			SCtmp->result = errstatus;
			my_done = ecbptr->done;
			memset(ecbptr,0,sizeof(struct ecb)); 
			if ( my_done )
				my_done(SCtmp);
			break;
			
		case	G2INTST_HARDFAIL:
			printk(KERN_ALERT "aha1740 hardware failure!\n");
			panic("aha1740.c");	/* Goodbye */
			
		case	G2INTST_ASNEVENT:
			printk("aha1740 asynchronous event: %02x %02x %02x %02x %02x\n",
			       adapstat,
			       inb(MBOXIN0(base)),
			       inb(MBOXIN1(base)),
			       inb(MBOXIN2(base)),
			       inb(MBOXIN3(base))); /* Say What? */
			/* Host Ready -> Mailbox in complete */
			outb(G2CNTRL_HRDY,G2CNTRL(base));
			break;
			
		case	G2INTST_CMDGOOD:
			/* set immediate command success flag here: */
			break;
			
		case	G2INTST_CMDERROR:
			/* Set immediate command failure flag here: */
			break;
		}
		number_serviced++;
	}

	spin_unlock_irqrestore(host->host_lock, flags);
	return IRQ_RETVAL(handled);
}

static int aha1740_queuecommand(Scsi_Cmnd * SCpnt, void (*done)(Scsi_Cmnd *))
{
	unchar direction;
	unchar *cmd = (unchar *) SCpnt->cmnd;
	unchar target = scmd_id(SCpnt);
	struct aha1740_hostdata *host = HOSTDATA(SCpnt->device->host);
	unsigned long flags;
	void *buff = SCpnt->request_buffer;
	int bufflen = SCpnt->request_bufflen;
	dma_addr_t sg_dma;
	struct aha1740_sg *sgptr;
	int ecbno;
	DEB(int i);

	if(*cmd == REQUEST_SENSE) {
		SCpnt->result = 0;
		done(SCpnt); 
		return 0;
	}

#ifdef DEBUG
	if (*cmd == READ_10 || *cmd == WRITE_10)
		i = xscsi2int(cmd+2);
	else if (*cmd == READ_6 || *cmd == WRITE_6)
		i = scsi2int(cmd+2);
	else
		i = -1;
	printk("aha1740_queuecommand: dev %d cmd %02x pos %d len %d ",
	       target, *cmd, i, bufflen);
	printk("scsi cmd:");
	for (i = 0; i < SCpnt->cmd_len; i++) printk("%02x ", cmd[i]);
	printk("\n");
#endif

	/* locate an available ecb */
	spin_lock_irqsave(SCpnt->device->host->host_lock, flags);
	ecbno = host->last_ecb_used + 1; /* An optimization */
	if (ecbno >= AHA1740_ECBS)
		ecbno = 0;
	do {
		if (!host->ecb[ecbno].cmdw)
			break;
		ecbno++;
		if (ecbno >= AHA1740_ECBS)
			ecbno = 0;
	} while (ecbno != host->last_ecb_used);

	if (host->ecb[ecbno].cmdw)
		panic("Unable to find empty ecb for aha1740.\n");

	host->ecb[ecbno].cmdw = AHA1740CMD_INIT; /* SCSI Initiator Command
						    doubles as reserved flag */

	host->last_ecb_used = ecbno;    
	spin_unlock_irqrestore(SCpnt->device->host->host_lock, flags);

#ifdef DEBUG
	printk("Sending command (%d %x)...", ecbno, done);
#endif

	host->ecb[ecbno].cdblen = SCpnt->cmd_len; /* SCSI Command
						   * Descriptor Block
						   * Length */

	direction = 0;
	if (*cmd == READ_10 || *cmd == READ_6)
		direction = 1;
	else if (*cmd == WRITE_10 || *cmd == WRITE_6)
		direction = 0;

	memcpy(host->ecb[ecbno].cdb, cmd, SCpnt->cmd_len);

	SCpnt->host_scribble = dma_alloc_coherent (&host->edev->dev,
						   sizeof (struct aha1740_sg),
						   &sg_dma, GFP_ATOMIC);
	if(SCpnt->host_scribble == NULL) {
		printk(KERN_WARNING "aha1740: out of memory in queuecommand!\n");
		return 1;
	}
	sgptr = (struct aha1740_sg *) SCpnt->host_scribble;
	sgptr->sg_dma_addr = sg_dma;
    
	if (SCpnt->use_sg) {
		struct scatterlist * sgpnt;
		struct aha1740_chain * cptr;
		int i, count;
		DEB(unsigned char * ptr);

		host->ecb[ecbno].sg = 1;  /* SCSI Initiator Command
					   * w/scatter-gather*/
		sgpnt = (struct scatterlist *) SCpnt->request_buffer;
		cptr = sgptr->sg_chain;
		count = dma_map_sg (&host->edev->dev, sgpnt, SCpnt->use_sg,
				    SCpnt->sc_data_direction);
		for(i=0; i < count; i++) {
			cptr[i].datalen = sg_dma_len (sgpnt + i);
			cptr[i].dataptr = sg_dma_address (sgpnt + i);
		}
		host->ecb[ecbno].datalen = count*sizeof(struct aha1740_chain);
		host->ecb[ecbno].dataptr = sg_dma;
#ifdef DEBUG
		printk("cptr %x: ",cptr);
		ptr = (unsigned char *) cptr;
		for(i=0;i<24;i++) printk("%02x ", ptr[i]);
#endif
	} else {
		host->ecb[ecbno].datalen = bufflen;
		sgptr->buf_dma_addr =  dma_map_single (&host->edev->dev,
						       buff, bufflen,
						       DMA_BIDIRECTIONAL);
		host->ecb[ecbno].dataptr = sgptr->buf_dma_addr;
	}
	host->ecb[ecbno].lun = SCpnt->device->lun;
	host->ecb[ecbno].ses = 1; /* Suppress underrun errors */
	host->ecb[ecbno].dir = direction;
	host->ecb[ecbno].ars = 1; /* Yes, get the sense on an error */
	host->ecb[ecbno].senselen = 12;
	host->ecb[ecbno].senseptr = ecb_cpu_to_dma (SCpnt->device->host,
						    host->ecb[ecbno].sense);
	host->ecb[ecbno].statusptr = ecb_cpu_to_dma (SCpnt->device->host,
						     host->ecb[ecbno].status);
	host->ecb[ecbno].done = done;
	host->ecb[ecbno].SCpnt = SCpnt;
#ifdef DEBUG
	{
		int i;
		printk("aha1740_command: sending.. ");
		for (i = 0; i < sizeof(host->ecb[ecbno]) - 10; i++)
			printk("%02x ", ((unchar *)&host->ecb[ecbno])[i]);
	}
	printk("\n");
#endif
	if (done) {
	/* The Adaptec Spec says the card is so fast that the loops
           will only be executed once in the code below. Even if this
           was true with the fastest processors when the spec was
           written, it doesn't seem to be true with todays fast
           processors. We print a warning if the code is executed more
           often than LOOPCNT_WARN. If this happens, it should be
           investigated. If the count reaches LOOPCNT_MAX, we assume
           something is broken; since there is no way to return an
           error (the return value is ignored by the mid-level scsi
           layer) we have to panic (and maybe that's the best thing we
           can do then anyhow). */

#define LOOPCNT_WARN 10		/* excessive mbxout wait -> syslog-msg */
#define LOOPCNT_MAX 1000000	/* mbxout deadlock -> panic() after ~ 2 sec. */
		int loopcnt;
		unsigned int base = SCpnt->device->host->io_port;
		DEB(printk("aha1740[%d] critical section\n",ecbno));

		spin_lock_irqsave(SCpnt->device->host->host_lock, flags);
		for (loopcnt = 0; ; loopcnt++) {
			if (inb(G2STAT(base)) & G2STAT_MBXOUT) break;
			if (loopcnt == LOOPCNT_WARN) {
				printk("aha1740[%d]_mbxout wait!\n",ecbno);
			}
			if (loopcnt == LOOPCNT_MAX)
				panic("aha1740.c: mbxout busy!\n");
		}
		outl (ecb_cpu_to_dma (SCpnt->device->host, host->ecb + ecbno),
		      MBOXOUT0(base));
		for (loopcnt = 0; ; loopcnt++) {
			if (! (inb(G2STAT(base)) & G2STAT_BUSY)) break;
			if (loopcnt == LOOPCNT_WARN) {
				printk("aha1740[%d]_attn wait!\n",ecbno);
			}
			if (loopcnt == LOOPCNT_MAX)
				panic("aha1740.c: attn wait failed!\n");
		}
		outb(ATTN_START | (target & 7), ATTN(base)); /* Start it up */
		spin_unlock_irqrestore(SCpnt->device->host->host_lock, flags);
		DEB(printk("aha1740[%d] request queued.\n",ecbno));
	} else
		printk(KERN_ALERT "aha1740_queuecommand: done can't be NULL\n");
	return 0;
}

/* Query the board for its irq_level and irq_type.  Nothing else matters
   in enhanced mode on an EISA bus. */

static void aha1740_getconfig(unsigned int base, unsigned int *irq_level,
			      unsigned int *irq_type,
			      unsigned int *translation)
{
	static int intab[] = { 9, 10, 11, 12, 0, 14, 15, 0 };

	*irq_level = intab[inb(INTDEF(base)) & 0x7];
	*irq_type  = (inb(INTDEF(base)) & 0x8) >> 3;
	*translation = inb(RESV1(base)) & 0x1;
	outb(inb(INTDEF(base)) | 0x10, INTDEF(base));
}

static int aha1740_biosparam(struct scsi_device *sdev,
			     struct block_device *dev,
			     sector_t capacity, int* ip)
{
	int size = capacity;
	int extended = HOSTDATA(sdev->host)->translation;

	DEB(printk("aha1740_biosparam\n"));
	if (extended && (ip[2] > 1024))	{
		ip[0] = 255;
		ip[1] = 63;
		ip[2] = size / (255 * 63);
	} else {
		ip[0] = 64;
		ip[1] = 32;
		ip[2] = size >> 11;
	}
	return 0;
}

static int aha1740_eh_abort_handler (Scsi_Cmnd *dummy)
{
/*
 * From Alan Cox :
 * The AHA1740 has firmware handled abort/reset handling. The "head in
 * sand" kernel code is correct for once 8)
 *
 * So we define a dummy handler just to keep the kernel SCSI code as
 * quiet as possible...
 */

	return 0;
}

static Scsi_Host_Template aha1740_template = {
	.module           = THIS_MODULE,
	.proc_name        = "aha1740",
	.proc_info        = aha1740_proc_info,
	.name             = "Adaptec 174x (EISA)",
	.queuecommand     = aha1740_queuecommand,
	.bios_param       = aha1740_biosparam,
	.can_queue        = AHA1740_ECBS,
	.this_id          = 7,
	.sg_tablesize     = AHA1740_SCATTER,
	.cmd_per_lun      = AHA1740_CMDLUN,
	.use_clustering   = ENABLE_CLUSTERING,
	.eh_abort_handler = aha1740_eh_abort_handler,
};

static int aha1740_probe (struct device *dev)
{
	int slotbase;
	unsigned int irq_level, irq_type, translation;
	struct Scsi_Host *shpnt;
	struct aha1740_hostdata *host;
	struct eisa_device *edev = to_eisa_device (dev);

	DEB(printk("aha1740_probe: \n"));
	
	slotbase = edev->base_addr + EISA_VENDOR_ID_OFFSET;
	if (!request_region(slotbase, SLOTSIZE, "aha1740")) /* See if in use */
		return -EBUSY;
	if (!aha1740_test_port(slotbase))
		goto err_release_region;
	aha1740_getconfig(slotbase,&irq_level,&irq_type,&translation);
	if ((inb(G2STAT(slotbase)) &
	     (G2STAT_MBXOUT|G2STAT_BUSY)) != G2STAT_MBXOUT) {
		/* If the card isn't ready, hard reset it */
		outb(G2CNTRL_HRST, G2CNTRL(slotbase));
		outb(0, G2CNTRL(slotbase));
	}
	printk(KERN_INFO "Configuring slot %d at IO:%x, IRQ %u (%s)\n",
	       edev->slot, slotbase, irq_level, irq_type ? "edge" : "level");
	printk(KERN_INFO "aha174x: Extended translation %sabled.\n",
	       translation ? "en" : "dis");
	shpnt = scsi_host_alloc(&aha1740_template,
			      sizeof(struct aha1740_hostdata));
	if(shpnt == NULL)
		goto err_release_region;

	shpnt->base = 0;
	shpnt->io_port = slotbase;
	shpnt->n_io_port = SLOTSIZE;
	shpnt->irq = irq_level;
	shpnt->dma_channel = 0xff;
	host = HOSTDATA(shpnt);
	host->edev = edev;
	host->translation = translation;
	host->ecb_dma_addr = dma_map_single (&edev->dev, host->ecb,
					     sizeof (host->ecb),
					     DMA_BIDIRECTIONAL);
	if (!host->ecb_dma_addr) {
		printk (KERN_ERR "aha1740_probe: Couldn't map ECB, giving up\n");
		scsi_unregister (shpnt);
		goto err_host_put;
	}
	
	DEB(printk("aha1740_probe: enable interrupt channel %d\n",irq_level));
	if (request_irq(irq_level,aha1740_intr_handle,irq_type ? 0 : SA_SHIRQ,
			"aha1740",shpnt)) {
		printk(KERN_ERR "aha1740_probe: Unable to allocate IRQ %d.\n",
		       irq_level);
		goto err_unmap;
	}

	eisa_set_drvdata (edev, shpnt);
	scsi_add_host (shpnt, dev); /* XXX handle failure */
	scsi_scan_host (shpnt);
	return 0;

 err_unmap:
	dma_unmap_single (&edev->dev, host->ecb_dma_addr,
			  sizeof (host->ecb), DMA_BIDIRECTIONAL);
 err_host_put:
	scsi_host_put (shpnt);
 err_release_region:
	release_region(slotbase, SLOTSIZE);

	return -ENODEV;
}

static __devexit int aha1740_remove (struct device *dev)
{
	struct Scsi_Host *shpnt = dev->driver_data;
	struct aha1740_hostdata *host = HOSTDATA (shpnt);

	scsi_remove_host(shpnt);
	
	free_irq (shpnt->irq, shpnt);
	dma_unmap_single (dev, host->ecb_dma_addr,
			  sizeof (host->ecb), DMA_BIDIRECTIONAL);
	release_region (shpnt->io_port, SLOTSIZE);

	scsi_host_put (shpnt);
	
	return 0;
}

static struct eisa_device_id aha1740_ids[] = {
	{ "ADP0000" },		/* 1740  */
	{ "ADP0001" },		/* 1740A */
	{ "ADP0002" },		/* 1742A */
	{ "ADP0400" },		/* 1744  */
	{ "" }
};

static struct eisa_driver aha1740_driver = {
	.id_table = aha1740_ids,
	.driver   = {
		.name    = "aha1740",
		.probe   = aha1740_probe,
		.remove  = __devexit_p (aha1740_remove),
	},
};

static __init int aha1740_init (void)
{
	return eisa_driver_register (&aha1740_driver);
}

static __exit void aha1740_exit (void)
{
	eisa_driver_unregister (&aha1740_driver);
}

module_init (aha1740_init);
module_exit (aha1740_exit);

MODULE_LICENSE("GPL");
