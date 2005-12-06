/*----------------------------------------------------------------*/
/*
   Qlogic linux driver - work in progress. No Warranty express or implied.
   Use at your own risk.  Support Tort Reform so you won't have to read all
   these silly disclaimers.

   Copyright 1994, Tom Zerucha.   
   tz@execpc.com
   
   Additional Code, and much appreciated help by
   Michael A. Griffith
   grif@cs.ucr.edu

   Thanks to Eric Youngdale and Dave Hinds for loadable module and PCMCIA
   help respectively, and for suffering through my foolishness during the
   debugging process.

   Reference Qlogic FAS408 Technical Manual, 53408-510-00A, May 10, 1994
   (you can reference it, but it is incomplete and inaccurate in places)

   Version 0.46 1/30/97 - kernel 1.2.0+

   Functions as standalone, loadable, and PCMCIA driver, the latter from
   Dave Hinds' PCMCIA package.
   
   Cleaned up 26/10/2002 by Alan Cox <alan@redhat.com> as part of the 2.5
   SCSI driver cleanup and audit. This driver still needs work on the
   following
   	-	Non terminating hardware waits
   	-	Some layering violations with its pcmcia stub

   Redistributable under terms of the GNU General Public License

   For the avoidance of doubt the "preferred form" of this code is one which
   is in an open non patent encumbered format. Where cryptographic key signing
   forms part of the process of creating an executable the information
   including keys needed to generate an equivalently functional executable
   are deemed to be part of the source code.

*/

#include <linux/module.h>
#include <linux/blkdev.h>		/* to get disk capacity */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/unistd.h>
#include <linux/spinlock.h>
#include <linux/stat.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dma.h>

#include "scsi.h"
#include <scsi/scsi_host.h>
#include "qlogicfas408.h"

/*----------------------------------------------------------------*/
static int qlcfg5 = (XTALFREQ << 5);	/* 15625/512 */
static int qlcfg6 = SYNCXFRPD;
static int qlcfg7 = SYNCOFFST;
static int qlcfg8 = (SLOWCABLE << 7) | (QL_ENABLE_PARITY << 4);
static int qlcfg9 = ((XTALFREQ + 4) / 5);
static int qlcfgc = (FASTCLK << 3) | (FASTSCSI << 4);

/*----------------------------------------------------------------*/

/*----------------------------------------------------------------*/
/* local functions */
/*----------------------------------------------------------------*/

/* error recovery - reset everything */

static void ql_zap(struct qlogicfas408_priv *priv)
{
	int x;
	int qbase = priv->qbase;
	int int_type = priv->int_type;

	x = inb(qbase + 0xd);
	REG0;
	outb(3, qbase + 3);	/* reset SCSI */
	outb(2, qbase + 3);	/* reset chip */
	if (x & 0x80)
		REG1;
}

/*
 *	Do a pseudo-dma tranfer
 */
 
static int ql_pdma(struct qlogicfas408_priv *priv, int phase, char *request, int reqlen)
{
	int j;
	int qbase = priv->qbase;
	j = 0;
	if (phase & 1) {	/* in */
#if QL_TURBO_PDMA
		rtrc(4)
		/* empty fifo in large chunks */
		if (reqlen >= 128 && (inb(qbase + 8) & 2)) {	/* full */
			insl(qbase + 4, request, 32);
			reqlen -= 128;
			request += 128;
		}
		while (reqlen >= 84 && !(j & 0xc0))	/* 2/3 */
			if ((j = inb(qbase + 8)) & 4) 
			{
				insl(qbase + 4, request, 21);
				reqlen -= 84;
				request += 84;
			}
		if (reqlen >= 44 && (inb(qbase + 8) & 8)) {	/* 1/3 */
			insl(qbase + 4, request, 11);
			reqlen -= 44;
			request += 44;
		}
#endif
		/* until both empty and int (or until reclen is 0) */
		rtrc(7)
		j = 0;
		while (reqlen && !((j & 0x10) && (j & 0xc0))) 
		{
			/* while bytes to receive and not empty */
			j &= 0xc0;
			while (reqlen && !((j = inb(qbase + 8)) & 0x10)) 
			{
				*request++ = inb(qbase + 4);
				reqlen--;
			}
			if (j & 0x10)
				j = inb(qbase + 8);

		}
	} else {		/* out */
#if QL_TURBO_PDMA
		rtrc(4)
		    if (reqlen >= 128 && inb(qbase + 8) & 0x10) {	/* empty */
			outsl(qbase + 4, request, 32);
			reqlen -= 128;
			request += 128;
		}
		while (reqlen >= 84 && !(j & 0xc0))	/* 1/3 */
			if (!((j = inb(qbase + 8)) & 8)) {
				outsl(qbase + 4, request, 21);
				reqlen -= 84;
				request += 84;
			}
		if (reqlen >= 40 && !(inb(qbase + 8) & 4)) {	/* 2/3 */
			outsl(qbase + 4, request, 10);
			reqlen -= 40;
			request += 40;
		}
#endif
		/* until full and int (or until reclen is 0) */
		rtrc(7)
		    j = 0;
		while (reqlen && !((j & 2) && (j & 0xc0))) {
			/* while bytes to send and not full */
			while (reqlen && !((j = inb(qbase + 8)) & 2)) 
			{
				outb(*request++, qbase + 4);
				reqlen--;
			}
			if (j & 2)
				j = inb(qbase + 8);
		}
	}
	/* maybe return reqlen */
	return inb(qbase + 8) & 0xc0;
}

/*
 *	Wait for interrupt flag (polled - not real hardware interrupt) 
 */

static int ql_wai(struct qlogicfas408_priv *priv)
{
	int k;
	int qbase = priv->qbase;
	unsigned long i;

	k = 0;
	i = jiffies + WATCHDOG;
	while (time_before(jiffies, i) && !priv->qabort &&
					!((k = inb(qbase + 4)) & 0xe0)) {
		barrier();
		cpu_relax();
	}
	if (time_after_eq(jiffies, i))
		return (DID_TIME_OUT);
	if (priv->qabort)
		return (priv->qabort == 1 ? DID_ABORT : DID_RESET);
	if (k & 0x60)
		ql_zap(priv);
	if (k & 0x20)
		return (DID_PARITY);
	if (k & 0x40)
		return (DID_ERROR);
	return 0;
}

/*
 *	Initiate scsi command - queueing handler 
 *	caller must hold host lock
 */

static void ql_icmd(Scsi_Cmnd * cmd)
{
	struct qlogicfas408_priv *priv = get_priv_by_cmd(cmd);
	int 	qbase = priv->qbase;
	int	int_type = priv->int_type;
	unsigned int i;

	priv->qabort = 0;

	REG0;
	/* clearing of interrupts and the fifo is needed */

	inb(qbase + 5);		/* clear interrupts */
	if (inb(qbase + 5))	/* if still interrupting */
		outb(2, qbase + 3);	/* reset chip */
	else if (inb(qbase + 7) & 0x1f)
		outb(1, qbase + 3);	/* clear fifo */
	while (inb(qbase + 5));	/* clear ints */
	REG1;
	outb(1, qbase + 8);	/* set for PIO pseudo DMA */
	outb(0, qbase + 0xb);	/* disable ints */
	inb(qbase + 8);		/* clear int bits */
	REG0;
	outb(0x40, qbase + 0xb);	/* enable features */

	/* configurables */
	outb(qlcfgc, qbase + 0xc);
	/* config: no reset interrupt, (initiator) bus id */
	outb(0x40 | qlcfg8 | priv->qinitid, qbase + 8);
	outb(qlcfg7, qbase + 7);
	outb(qlcfg6, qbase + 6);
	 /**/ outb(qlcfg5, qbase + 5);	/* select timer */
	outb(qlcfg9 & 7, qbase + 9);	/* prescaler */
/*	outb(0x99, qbase + 5);	*/
	outb(scmd_id(cmd), qbase + 4);

	for (i = 0; i < cmd->cmd_len; i++)
		outb(cmd->cmnd[i], qbase + 2);

	priv->qlcmd = cmd;
	outb(0x41, qbase + 3);	/* select and send command */
}

/*
 *	Process scsi command - usually after interrupt 
 */

static unsigned int ql_pcmd(Scsi_Cmnd * cmd)
{
	unsigned int i, j;
	unsigned long k;
	unsigned int result;	/* ultimate return result */
	unsigned int status;	/* scsi returned status */
	unsigned int message;	/* scsi returned message */
	unsigned int phase;	/* recorded scsi phase */
	unsigned int reqlen;	/* total length of transfer */
	struct scatterlist *sglist;	/* scatter-gather list pointer */
	unsigned int sgcount;	/* sg counter */
	char *buf;
	struct qlogicfas408_priv *priv = get_priv_by_cmd(cmd);
	int qbase = priv->qbase;
	int int_type = priv->int_type;

	rtrc(1)
	j = inb(qbase + 6);
	i = inb(qbase + 5);
	if (i == 0x20) {
		return (DID_NO_CONNECT << 16);
	}
	i |= inb(qbase + 5);	/* the 0x10 bit can be set after the 0x08 */
	if (i != 0x18) {
		printk(KERN_ERR "Ql:Bad Interrupt status:%02x\n", i);
		ql_zap(priv);
		return (DID_BAD_INTR << 16);
	}
	j &= 7;			/* j = inb( qbase + 7 ) >> 5; */

	/* correct status is supposed to be step 4 */
	/* it sometimes returns step 3 but with 0 bytes left to send */
	/* We can try stuffing the FIFO with the max each time, but we will get a
	   sequence of 3 if any bytes are left (but we do flush the FIFO anyway */

	if (j != 3 && j != 4) {
		printk(KERN_ERR "Ql:Bad sequence for command %d, int %02X, cmdleft = %d\n",
		     j, i, inb(qbase + 7) & 0x1f);
		ql_zap(priv);
		return (DID_ERROR << 16);
	}
	result = DID_OK;
	if (inb(qbase + 7) & 0x1f)	/* if some bytes in fifo */
		outb(1, qbase + 3);	/* clear fifo */
	/* note that request_bufflen is the total xfer size when sg is used */
	reqlen = cmd->request_bufflen;
	/* note that it won't work if transfers > 16M are requested */
	if (reqlen && !((phase = inb(qbase + 4)) & 6)) {	/* data phase */
		rtrc(2)
		outb(reqlen, qbase);	/* low-mid xfer cnt */
		outb(reqlen >> 8, qbase + 1);	/* low-mid xfer cnt */
		outb(reqlen >> 16, qbase + 0xe);	/* high xfer cnt */
		outb(0x90, qbase + 3);	/* command do xfer */
		/* PIO pseudo DMA to buffer or sglist */
		REG1;
		if (!cmd->use_sg)
			ql_pdma(priv, phase, cmd->request_buffer,
				cmd->request_bufflen);
		else {
			sgcount = cmd->use_sg;
			sglist = cmd->request_buffer;
			while (sgcount--) {
				if (priv->qabort) {
					REG0;
					return ((priv->qabort == 1 ?
						DID_ABORT : DID_RESET) << 16);
				}
				buf = page_address(sglist->page) + sglist->offset;
				if (ql_pdma(priv, phase, buf, sglist->length))
					break;
				sglist++;
			}
		}
		REG0;
		rtrc(2)
		/*
		 *	Wait for irq (split into second state of irq handler
		 *	if this can take time) 
		 */
		if ((k = ql_wai(priv)))
			return (k << 16);
		k = inb(qbase + 5);	/* should be 0x10, bus service */
	}

	/*
	 *	Enter Status (and Message In) Phase 
	 */
	 
	k = jiffies + WATCHDOG;

	while (time_before(jiffies, k) && !priv->qabort &&
						!(inb(qbase + 4) & 6))
		cpu_relax();	/* wait for status phase */

	if (time_after_eq(jiffies, k)) {
		ql_zap(priv);
		return (DID_TIME_OUT << 16);
	}

	/* FIXME: timeout ?? */
	while (inb(qbase + 5))
		cpu_relax();	/* clear pending ints */

	if (priv->qabort)
		return ((priv->qabort == 1 ? DID_ABORT : DID_RESET) << 16);

	outb(0x11, qbase + 3);	/* get status and message */
	if ((k = ql_wai(priv)))
		return (k << 16);
	i = inb(qbase + 5);	/* get chip irq stat */
	j = inb(qbase + 7) & 0x1f;	/* and bytes rec'd */
	status = inb(qbase + 2);
	message = inb(qbase + 2);

	/*
	 *	Should get function complete int if Status and message, else 
	 *	bus serv if only status 
	 */
	if (!((i == 8 && j == 2) || (i == 0x10 && j == 1))) {
		printk(KERN_ERR "Ql:Error during status phase, int=%02X, %d bytes recd\n", i, j);
		result = DID_ERROR;
	}
	outb(0x12, qbase + 3);	/* done, disconnect */
	rtrc(1)
	if ((k = ql_wai(priv)))
		return (k << 16);

	/*
	 *	Should get bus service interrupt and disconnect interrupt 
	 */
	 
	i = inb(qbase + 5);	/* should be bus service */
	while (!priv->qabort && ((i & 0x20) != 0x20)) {
		barrier();
		cpu_relax();
		i |= inb(qbase + 5);
	}
	rtrc(0)

	if (priv->qabort)
		return ((priv->qabort == 1 ? DID_ABORT : DID_RESET) << 16);
		
	return (result << 16) | (message << 8) | (status & STATUS_MASK);
}

/*
 *	Interrupt handler 
 */

static void ql_ihandl(int irq, void *dev_id, struct pt_regs *regs)
{
	Scsi_Cmnd *icmd;
	struct Scsi_Host *host = (struct Scsi_Host *)dev_id;
	struct qlogicfas408_priv *priv = get_priv_by_host(host);
	int qbase = priv->qbase;
	REG0;

	if (!(inb(qbase + 4) & 0x80))	/* false alarm? */
		return;

	if (priv->qlcmd == NULL) {	/* no command to process? */
		int i;
		i = 16;
		while (i-- && inb(qbase + 5));	/* maybe also ql_zap() */
		return;
	}
	icmd = priv->qlcmd;
	icmd->result = ql_pcmd(icmd);
	priv->qlcmd = NULL;
	/*
	 *	If result is CHECK CONDITION done calls qcommand to request 
	 *	sense 
	 */
	(icmd->scsi_done) (icmd);
}

irqreturn_t qlogicfas408_ihandl(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	struct Scsi_Host *host = dev_id;

	spin_lock_irqsave(host->host_lock, flags);
	ql_ihandl(irq, dev_id, regs);
	spin_unlock_irqrestore(host->host_lock, flags);
	return IRQ_HANDLED;
}

/*
 *	Queued command
 */

int qlogicfas408_queuecommand(Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))
{
	struct qlogicfas408_priv *priv = get_priv_by_cmd(cmd);
	if (scmd_id(cmd) == priv->qinitid) {
		cmd->result = DID_BAD_TARGET << 16;
		done(cmd);
		return 0;
	}

	cmd->scsi_done = done;
	/* wait for the last command's interrupt to finish */
	while (priv->qlcmd != NULL) {
		barrier();
		cpu_relax();
	}
	ql_icmd(cmd);
	return 0;
}

/* 
 *	Return bios parameters 
 */

int qlogicfas408_biosparam(struct scsi_device * disk,
		        struct block_device *dev,
			sector_t capacity, int ip[])
{
/* This should mimic the DOS Qlogic driver's behavior exactly */
	ip[0] = 0x40;
	ip[1] = 0x20;
	ip[2] = (unsigned long) capacity / (ip[0] * ip[1]);
	if (ip[2] > 1024) {
		ip[0] = 0xff;
		ip[1] = 0x3f;
		ip[2] = (unsigned long) capacity / (ip[0] * ip[1]);
#if 0
		if (ip[2] > 1023)
			ip[2] = 1023;
#endif
	}
	return 0;
}

/*
 *	Abort a command in progress
 */
 
int qlogicfas408_abort(Scsi_Cmnd * cmd)
{
	struct qlogicfas408_priv *priv = get_priv_by_cmd(cmd);
	priv->qabort = 1;
	ql_zap(priv);
	return SUCCESS;
}

/* 
 *	Reset SCSI bus
 *	FIXME: This function is invoked with cmd = NULL directly by
 *	the PCMCIA qlogic_stub code. This wants fixing
 */

int qlogicfas408_bus_reset(Scsi_Cmnd * cmd)
{
	struct qlogicfas408_priv *priv = get_priv_by_cmd(cmd);
	unsigned long flags;

	priv->qabort = 2;

	spin_lock_irqsave(cmd->device->host->host_lock, flags);
	ql_zap(priv);
	spin_unlock_irqrestore(cmd->device->host->host_lock, flags);

	return SUCCESS;
}

/*
 *	Return info string
 */

const char *qlogicfas408_info(struct Scsi_Host *host)
{
	struct qlogicfas408_priv *priv = get_priv_by_host(host);
	return priv->qinfo;
}

/*
 *	Get type of chip
 */

int qlogicfas408_get_chip_type(int qbase, int int_type)
{
	REG1;
	return inb(qbase + 0xe) & 0xf8;
}

/*
 *	Perform initialization tasks
 */

void qlogicfas408_setup(int qbase, int id, int int_type)
{
	outb(1, qbase + 8);	/* set for PIO pseudo DMA */
	REG0;
	outb(0x40 | qlcfg8 | id, qbase + 8);	/* (ini) bus id, disable scsi rst */
	outb(qlcfg5, qbase + 5);	/* select timer */
	outb(qlcfg9, qbase + 9);	/* prescaler */

#if QL_RESET_AT_START
	outb(3, qbase + 3);

	REG1;
	/* FIXME: timeout */
	while (inb(qbase + 0xf) & 4)
		cpu_relax();

	REG0;
#endif
}

/*
 *	Checks if this is a QLogic FAS 408
 */

int qlogicfas408_detect(int qbase, int int_type)
{
        REG1;
	return (((inb(qbase + 0xe) ^ inb(qbase + 0xe)) == 7) &&
	       ((inb(qbase + 0xe) ^ inb(qbase + 0xe)) == 7));		
}

/*
 *	Disable interrupts
 */

void qlogicfas408_disable_ints(struct qlogicfas408_priv *priv)
{
	int qbase = priv->qbase;
	int int_type = priv->int_type;

	REG1;
	outb(0, qbase + 0xb);	/* disable ints */
}

/*
 *	Init and exit functions
 */

static int __init qlogicfas408_init(void)
{
	return 0;
}

static void __exit qlogicfas408_exit(void)
{

}

MODULE_AUTHOR("Tom Zerucha, Michael Griffith");
MODULE_DESCRIPTION("Driver for the Qlogic FAS SCSI controllers");
MODULE_LICENSE("GPL");
module_init(qlogicfas408_init);
module_exit(qlogicfas408_exit);

EXPORT_SYMBOL(qlogicfas408_info);
EXPORT_SYMBOL(qlogicfas408_queuecommand);
EXPORT_SYMBOL(qlogicfas408_abort);
EXPORT_SYMBOL(qlogicfas408_bus_reset);
EXPORT_SYMBOL(qlogicfas408_biosparam);
EXPORT_SYMBOL(qlogicfas408_ihandl);
EXPORT_SYMBOL(qlogicfas408_get_chip_type);
EXPORT_SYMBOL(qlogicfas408_setup);
EXPORT_SYMBOL(qlogicfas408_detect);
EXPORT_SYMBOL(qlogicfas408_disable_ints);

