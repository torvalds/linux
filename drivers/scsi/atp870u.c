/* 
 *  Copyright (C) 1997	Wu Ching Chen
 *  2.1.x update (C) 1998  Krzysztof G. Baranowski
 *  2.5.x update (C) 2002  Red Hat
 *  2.6.x update (C) 2004  Red Hat
 *
 * Marcelo Tosatti <marcelo@conectiva.com.br> : SMP fixes
 *
 * Wu Ching Chen : NULL pointer fixes  2000/06/02
 *		   support atp876 chip
 *		   enable 32 bit fifo transfer
 *		   support cdrom & remove device run ultra speed
 *		   fix disconnect bug  2000/12/21
 *		   support atp880 chip lvd u160 2001/05/15
 *		   fix prd table bug 2001/09/12 (7.1)
 *
 * atp885 support add by ACARD Hao Ping Lian 2005/01/05
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/blkdev.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <asm/io.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "atp870u.h"

static struct scsi_host_template atp870u_template;
static void send_s870(struct atp_unit *dev,unsigned char c);
static void atp_is(struct atp_unit *dev, unsigned char c, bool wide_chip, unsigned char lvdmode);

static inline void atp_writeb_base(struct atp_unit *atp, u8 reg, u8 val)
{
	outb(val, atp->baseport + reg);
}

static inline void atp_writew_base(struct atp_unit *atp, u8 reg, u16 val)
{
	outw(val, atp->baseport + reg);
}

static inline void atp_writeb_io(struct atp_unit *atp, u8 channel, u8 reg, u8 val)
{
	outb(val, atp->ioport[channel] + reg);
}

static inline void atp_writew_io(struct atp_unit *atp, u8 channel, u8 reg, u16 val)
{
	outw(val, atp->ioport[channel] + reg);
}

static inline void atp_writeb_pci(struct atp_unit *atp, u8 channel, u8 reg, u8 val)
{
	outb(val, atp->pciport[channel] + reg);
}

static inline void atp_writel_pci(struct atp_unit *atp, u8 channel, u8 reg, u32 val)
{
	outl(val, atp->pciport[channel] + reg);
}

static inline u8 atp_readb_base(struct atp_unit *atp, u8 reg)
{
	return inb(atp->baseport + reg);
}

static inline u16 atp_readw_base(struct atp_unit *atp, u8 reg)
{
	return inw(atp->baseport + reg);
}

static inline u32 atp_readl_base(struct atp_unit *atp, u8 reg)
{
	return inl(atp->baseport + reg);
}

static inline u8 atp_readb_io(struct atp_unit *atp, u8 channel, u8 reg)
{
	return inb(atp->ioport[channel] + reg);
}

static inline u16 atp_readw_io(struct atp_unit *atp, u8 channel, u8 reg)
{
	return inw(atp->ioport[channel] + reg);
}

static inline u8 atp_readb_pci(struct atp_unit *atp, u8 channel, u8 reg)
{
	return inb(atp->pciport[channel] + reg);
}

static inline bool is880(struct atp_unit *atp)
{
	return atp->pdev->device == ATP880_DEVID1 ||
	       atp->pdev->device == ATP880_DEVID2;
}

static inline bool is885(struct atp_unit *atp)
{
	return atp->pdev->device == ATP885_DEVID;
}

static irqreturn_t atp870u_intr_handle(int irq, void *dev_id)
{
	unsigned long flags;
	unsigned short int id;
	unsigned char i, j, c, target_id, lun,cmdp;
	unsigned char *prd;
	struct scsi_cmnd *workreq;
	unsigned long adrcnt, k;
#ifdef ED_DBGP
	unsigned long l;
#endif
	struct Scsi_Host *host = dev_id;
	struct atp_unit *dev = (struct atp_unit *)&host->hostdata;

	for (c = 0; c < 2; c++) {
		j = atp_readb_io(dev, c, 0x1f);
		if ((j & 0x80) != 0)
			break;
		dev->in_int[c] = 0;
	}
	if ((j & 0x80) == 0)
		return IRQ_NONE;
#ifdef ED_DBGP	
	printk("atp870u_intr_handle enter\n");
#endif	
	dev->in_int[c] = 1;
	cmdp = atp_readb_io(dev, c, 0x10);
	if (dev->working[c] != 0) {
		if (is885(dev)) {
			if ((atp_readb_io(dev, c, 0x16) & 0x80) == 0)
				atp_writeb_io(dev, c, 0x16, (atp_readb_io(dev, c, 0x16) | 0x80));
		}		
		if ((atp_readb_pci(dev, c, 0x00) & 0x08) != 0)
		{
			for (k=0; k < 1000; k++) {
				if ((atp_readb_pci(dev, c, 2) & 0x08) == 0)
					break;
				if ((atp_readb_pci(dev, c, 2) & 0x01) == 0)
					break;
			}
		}
		atp_writeb_pci(dev, c, 0, 0x00);
		
		i = atp_readb_io(dev, c, 0x17);
		
		if (is885(dev))
			atp_writeb_pci(dev, c, 2, 0x06);

		target_id = atp_readb_io(dev, c, 0x15);

		/*
		 *	Remap wide devices onto id numbers
		 */

		if ((target_id & 0x40) != 0) {
			target_id = (target_id & 0x07) | 0x08;
		} else {
			target_id &= 0x07;
		}

		if ((j & 0x40) != 0) {
		     if (dev->last_cmd[c] == 0xff) {
			dev->last_cmd[c] = target_id;
		     }
		     dev->last_cmd[c] |= 0x40;
		}
		if (is885(dev))
			dev->r1f[c][target_id] |= j;
#ifdef ED_DBGP
		printk("atp870u_intr_handle status = %x\n",i);
#endif	
		if (i == 0x85) {
			if ((dev->last_cmd[c] & 0xf0) != 0x40) {
			   dev->last_cmd[c] = 0xff;
			}
			if (is885(dev)) {
				adrcnt = 0;
				((unsigned char *) &adrcnt)[2] = atp_readb_io(dev, c, 0x12);
				((unsigned char *) &adrcnt)[1] = atp_readb_io(dev, c, 0x13);
				((unsigned char *) &adrcnt)[0] = atp_readb_io(dev, c, 0x14);
				if (dev->id[c][target_id].last_len != adrcnt)
				{
			   		k = dev->id[c][target_id].last_len;
			   		k -= adrcnt;
			   		dev->id[c][target_id].tran_len = k;			   
			   	dev->id[c][target_id].last_len = adrcnt;			   
				}
#ifdef ED_DBGP
				printk("dev->id[c][target_id].last_len = %d dev->id[c][target_id].tran_len = %d\n",dev->id[c][target_id].last_len,dev->id[c][target_id].tran_len);
#endif		
			}

			/*
			 *      Flip wide
			 */			
			if (dev->wide_id[c] != 0) {
				atp_writeb_io(dev, c, 0x1b, 0x01);
				while ((atp_readb_io(dev, c, 0x1b) & 0x01) != 0x01)
					atp_writeb_io(dev, c, 0x1b, 0x01);
			}		
			/*
			 *	Issue more commands
			 */
			spin_lock_irqsave(dev->host->host_lock, flags);			 			 
			if (((dev->quhd[c] != dev->quend[c]) || (dev->last_cmd[c] != 0xff)) &&
			    (dev->in_snd[c] == 0)) {
#ifdef ED_DBGP
				printk("Call sent_s870\n");
#endif				
				send_s870(dev,c);
			}
			spin_unlock_irqrestore(dev->host->host_lock, flags);
			/*
			 *	Done
			 */
			dev->in_int[c] = 0;
#ifdef ED_DBGP
				printk("Status 0x85 return\n");
#endif				
			return IRQ_HANDLED;
		}

		if (i == 0x40) {
		     dev->last_cmd[c] |= 0x40;
		     dev->in_int[c] = 0;
		     return IRQ_HANDLED;
		}

		if (i == 0x21) {
			if ((dev->last_cmd[c] & 0xf0) != 0x40) {
			   dev->last_cmd[c] = 0xff;
			}
			adrcnt = 0;
			((unsigned char *) &adrcnt)[2] = atp_readb_io(dev, c, 0x12);
			((unsigned char *) &adrcnt)[1] = atp_readb_io(dev, c, 0x13);
			((unsigned char *) &adrcnt)[0] = atp_readb_io(dev, c, 0x14);
			k = dev->id[c][target_id].last_len;
			k -= adrcnt;
			dev->id[c][target_id].tran_len = k;
			dev->id[c][target_id].last_len = adrcnt;
			atp_writeb_io(dev, c, 0x10, 0x41);
			atp_writeb_io(dev, c, 0x18, 0x08);
			dev->in_int[c] = 0;
			return IRQ_HANDLED;
		}

		if (is885(dev)) {
			if ((i == 0x4c) || (i == 0x4d) || (i == 0x8c) || (i == 0x8d)) {
		   		if ((i == 0x4c) || (i == 0x8c)) 
		      			i=0x48;
		   		else 
		      			i=0x49;
		   	}	
			
		}
		if ((i == 0x80) || (i == 0x8f)) {
#ifdef ED_DBGP
			printk(KERN_DEBUG "Device reselect\n");
#endif			
			lun = 0;
			if (cmdp == 0x44 || i == 0x80)
				lun = atp_readb_io(dev, c, 0x1d) & 0x07;
			else {
				if ((dev->last_cmd[c] & 0xf0) != 0x40) {
				   dev->last_cmd[c] = 0xff;
				}
				if (cmdp == 0x41) {
#ifdef ED_DBGP
					printk("cmdp = 0x41\n");
#endif						
					adrcnt = 0;
					((unsigned char *) &adrcnt)[2] = atp_readb_io(dev, c, 0x12);
					((unsigned char *) &adrcnt)[1] = atp_readb_io(dev, c, 0x13);
					((unsigned char *) &adrcnt)[0] = atp_readb_io(dev, c, 0x14);
					k = dev->id[c][target_id].last_len;
					k -= adrcnt;
					dev->id[c][target_id].tran_len = k;
					dev->id[c][target_id].last_len = adrcnt;
					atp_writeb_io(dev, c, 0x18, 0x08);
					dev->in_int[c] = 0;
					return IRQ_HANDLED;
				} else {
#ifdef ED_DBGP
					printk("cmdp != 0x41\n");
#endif						
					atp_writeb_io(dev, c, 0x10, 0x46);
					dev->id[c][target_id].dirct = 0x00;
					atp_writeb_io(dev, c, 0x12, 0x00);
					atp_writeb_io(dev, c, 0x13, 0x00);
					atp_writeb_io(dev, c, 0x14, 0x00);
					atp_writeb_io(dev, c, 0x18, 0x08);
					dev->in_int[c] = 0;
					return IRQ_HANDLED;
				}
			}
			if (dev->last_cmd[c] != 0xff) {
			   dev->last_cmd[c] |= 0x40;
			}
			if (is885(dev)) {
				j = atp_readb_base(dev, 0x29) & 0xfe;
				atp_writeb_base(dev, 0x29, j);
			} else
				atp_writeb_io(dev, c, 0x10, 0x45);

			target_id = atp_readb_io(dev, c, 0x16);
			/*
			 *	Remap wide identifiers
			 */
			if ((target_id & 0x10) != 0) {
				target_id = (target_id & 0x07) | 0x08;
			} else {
				target_id &= 0x07;
			}
			if (is885(dev))
				atp_writeb_io(dev, c, 0x10, 0x45);
			workreq = dev->id[c][target_id].curr_req;
#ifdef ED_DBGP			
			scmd_printk(KERN_DEBUG, workreq, "CDB");
			for (l = 0; l < workreq->cmd_len; l++)
				printk(KERN_DEBUG " %x",workreq->cmnd[l]);
			printk("\n");
#endif	
			
			atp_writeb_io(dev, c, 0x0f, lun);
			atp_writeb_io(dev, c, 0x11, dev->id[c][target_id].devsp);
			adrcnt = dev->id[c][target_id].tran_len;
			k = dev->id[c][target_id].last_len;

			atp_writeb_io(dev, c, 0x12, ((unsigned char *) &k)[2]);
			atp_writeb_io(dev, c, 0x13, ((unsigned char *) &k)[1]);
			atp_writeb_io(dev, c, 0x14, ((unsigned char *) &k)[0]);
#ifdef ED_DBGP			
			printk("k %x, k[0] 0x%x k[1] 0x%x k[2] 0x%x\n", k, atp_readb_io(dev, c, 0x14), atp_readb_io(dev, c, 0x13), atp_readb_io(dev, c, 0x12));
#endif			
			/* Remap wide */
			j = target_id;
			if (target_id > 7) {
				j = (j & 0x07) | 0x40;
			}
			/* Add direction */
			j |= dev->id[c][target_id].dirct;
			atp_writeb_io(dev, c, 0x15, j);
			atp_writeb_io(dev, c, 0x16, 0x80);
			
			/* enable 32 bit fifo transfer */	
			if (is885(dev)) {
				i = atp_readb_pci(dev, c, 1) & 0xf3;
				//j=workreq->cmnd[0];	    		    	
				if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) || (workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a)) {
				   i |= 0x0c;
				}
				atp_writeb_pci(dev, c, 1, i);
			} else if (is880(dev)) {
				if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) || (workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a))
					atp_writeb_base(dev, 0x3b, (atp_readb_base(dev, 0x3b) & 0x3f) | 0xc0);
				else
					atp_writeb_base(dev, 0x3b, atp_readb_base(dev, 0x3b) & 0x3f);
			} else {				
				if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) || (workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a))
					atp_writeb_base(dev, 0x3a, (atp_readb_base(dev, 0x3a) & 0xf3) | 0x08);
				else
					atp_writeb_base(dev, 0x3a, atp_readb_base(dev, 0x3a) & 0xf3);
			}	
			j = 0;
			id = 1;
			id = id << target_id;
			/*
			 *	Is this a wide device
			 */
			if ((id & dev->wide_id[c]) != 0) {
				j |= 0x01;
			}
			atp_writeb_io(dev, c, 0x1b, j);
			while ((atp_readb_io(dev, c, 0x1b) & 0x01) != j)
				atp_writeb_io(dev, c, 0x1b, j);
			if (dev->id[c][target_id].last_len == 0) {
				atp_writeb_io(dev, c, 0x18, 0x08);
				dev->in_int[c] = 0;
#ifdef ED_DBGP
				printk("dev->id[c][target_id].last_len = 0\n");
#endif					
				return IRQ_HANDLED;
			}
#ifdef ED_DBGP
			printk("target_id = %d adrcnt = %d\n",target_id,adrcnt);
#endif			
			prd = dev->id[c][target_id].prd_pos;
			while (adrcnt != 0) {
				id = ((unsigned short int *)prd)[2];
				if (id == 0) {
					k = 0x10000;
				} else {
					k = id;
				}
				if (k > adrcnt) {
					((unsigned short int *)prd)[2] = (unsigned short int)
					    (k - adrcnt);
					((unsigned long *)prd)[0] += adrcnt;
					adrcnt = 0;
					dev->id[c][target_id].prd_pos = prd;
				} else {
					adrcnt -= k;
					dev->id[c][target_id].prdaddr += 0x08;
					prd += 0x08;
					if (adrcnt == 0) {
						dev->id[c][target_id].prd_pos = prd;
					}
				}				
			}
			atp_writel_pci(dev, c, 0x04, dev->id[c][target_id].prdaddr);
#ifdef ED_DBGP
			printk("dev->id[%d][%d].prdaddr 0x%8x\n", c, target_id, dev->id[c][target_id].prdaddr);
#endif
			if (!is885(dev)) {
				atp_writeb_pci(dev, c, 2, 0x06);
				atp_writeb_pci(dev, c, 2, 0x00);
			}
			/*
			 *	Check transfer direction
			 */
			if (dev->id[c][target_id].dirct != 0) {
				atp_writeb_io(dev, c, 0x18, 0x08);
				atp_writeb_pci(dev, c, 0, 0x01);
				dev->in_int[c] = 0;
#ifdef ED_DBGP
				printk("status 0x80 return dirct != 0\n");
#endif				
				return IRQ_HANDLED;
			}
			atp_writeb_io(dev, c, 0x18, 0x08);
			atp_writeb_pci(dev, c, 0, 0x09);
			dev->in_int[c] = 0;
#ifdef ED_DBGP
			printk("status 0x80 return dirct = 0\n");
#endif			
			return IRQ_HANDLED;
		}

		/*
		 *	Current scsi request on this target
		 */

		workreq = dev->id[c][target_id].curr_req;

		if (i == 0x42 || i == 0x16) {
			if ((dev->last_cmd[c] & 0xf0) != 0x40) {
			   dev->last_cmd[c] = 0xff;
			}
			if (i == 0x16) {
				workreq->result = atp_readb_io(dev, c, 0x0f);
				if (((dev->r1f[c][target_id] & 0x10) != 0) && is885(dev)) {
					printk(KERN_WARNING "AEC67162 CRC ERROR !\n");
					workreq->result = 0x02;
				}
			} else
				workreq->result = 0x02;

			if (is885(dev)) {
				j = atp_readb_base(dev, 0x29) | 0x01;
				atp_writeb_base(dev, 0x29, j);
			}
			/*
			 *	Complete the command
			 */
			scsi_dma_unmap(workreq);

			spin_lock_irqsave(dev->host->host_lock, flags);
			(*workreq->scsi_done) (workreq);
#ifdef ED_DBGP
			   printk("workreq->scsi_done\n");
#endif	
			/*
			 *	Clear it off the queue
			 */
			dev->id[c][target_id].curr_req = NULL;
			dev->working[c]--;
			spin_unlock_irqrestore(dev->host->host_lock, flags);
			/*
			 *      Take it back wide
			 */
			if (dev->wide_id[c] != 0) {
				atp_writeb_io(dev, c, 0x1b, 0x01);
				while ((atp_readb_io(dev, c, 0x1b) & 0x01) != 0x01)
					atp_writeb_io(dev, c, 0x1b, 0x01);
			} 
			/*
			 *	If there is stuff to send and nothing going then send it
			 */
			spin_lock_irqsave(dev->host->host_lock, flags);
			if (((dev->last_cmd[c] != 0xff) || (dev->quhd[c] != dev->quend[c])) &&
			    (dev->in_snd[c] == 0)) {
#ifdef ED_DBGP
			   printk("Call sent_s870(scsi_done)\n");
#endif				   
			   send_s870(dev,c);
			}
			spin_unlock_irqrestore(dev->host->host_lock, flags);
			dev->in_int[c] = 0;
			return IRQ_HANDLED;
		}
		if ((dev->last_cmd[c] & 0xf0) != 0x40) {
		   dev->last_cmd[c] = 0xff;
		}
		if (i == 0x4f) {
			i = 0x89;
		}
		i &= 0x0f;
		if (i == 0x09) {
			atp_writel_pci(dev, c, 4, dev->id[c][target_id].prdaddr);
			atp_writeb_pci(dev, c, 2, 0x06);
			atp_writeb_pci(dev, c, 2, 0x00);
			atp_writeb_io(dev, c, 0x10, 0x41);
			if (is885(dev)) {
				k = dev->id[c][target_id].last_len;
				atp_writeb_io(dev, c, 0x12, ((unsigned char *) (&k))[2]);
				atp_writeb_io(dev, c, 0x13, ((unsigned char *) (&k))[1]);
				atp_writeb_io(dev, c, 0x14, ((unsigned char *) (&k))[0]);
				dev->id[c][target_id].dirct = 0x00;
			} else {
				dev->id[c][target_id].dirct = 0x00;
			}
			atp_writeb_io(dev, c, 0x18, 0x08);
			atp_writeb_pci(dev, c, 0, 0x09);
			dev->in_int[c] = 0;
			return IRQ_HANDLED;
		}
		if (i == 0x08) {
			atp_writel_pci(dev, c, 4, dev->id[c][target_id].prdaddr);
			atp_writeb_pci(dev, c, 2, 0x06);
			atp_writeb_pci(dev, c, 2, 0x00);
			atp_writeb_io(dev, c, 0x10, 0x41);
			if (is885(dev)) {
				k = dev->id[c][target_id].last_len;
				atp_writeb_io(dev, c, 0x12, ((unsigned char *) (&k))[2]);
				atp_writeb_io(dev, c, 0x13, ((unsigned char *) (&k))[1]);
				atp_writeb_io(dev, c, 0x14, ((unsigned char *) (&k))[0]);
			}
			atp_writeb_io(dev, c, 0x15, atp_readb_io(dev, c, 0x15) | 0x20);
			dev->id[c][target_id].dirct = 0x20;
			atp_writeb_io(dev, c, 0x18, 0x08);
			atp_writeb_pci(dev, c, 0, 0x01);
			dev->in_int[c] = 0;
			return IRQ_HANDLED;
		}
		if (i == 0x0a)
			atp_writeb_io(dev, c, 0x10, 0x30);
		else
			atp_writeb_io(dev, c, 0x10, 0x46);
		dev->id[c][target_id].dirct = 0x00;
		atp_writeb_io(dev, c, 0x12, 0x00);
		atp_writeb_io(dev, c, 0x13, 0x00);
		atp_writeb_io(dev, c, 0x14, 0x00);
		atp_writeb_io(dev, c, 0x18, 0x08);
	}
	dev->in_int[c] = 0;

	return IRQ_HANDLED;
}
/**
 *	atp870u_queuecommand	-	Queue SCSI command
 *	@req_p: request block
 *	@done: completion function
 *
 *	Queue a command to the ATP queue. Called with the host lock held.
 */
static int atp870u_queuecommand_lck(struct scsi_cmnd *req_p,
			 void (*done) (struct scsi_cmnd *))
{
	unsigned char c;
	unsigned int m;
	struct atp_unit *dev;
	struct Scsi_Host *host;

	c = scmd_channel(req_p);
	req_p->sense_buffer[0]=0;
	scsi_set_resid(req_p, 0);
	if (scmd_channel(req_p) > 1) {
		req_p->result = 0x00040000;
		done(req_p);
#ifdef ED_DBGP		
		printk("atp870u_queuecommand : req_p->device->channel > 1\n");	
#endif			
		return 0;
	}

	host = req_p->device->host;
	dev = (struct atp_unit *)&host->hostdata;
		

		
	m = 1;
	m = m << scmd_id(req_p);

	/*
	 *      Fake a timeout for missing targets
	 */

	if ((m & dev->active_id[c]) == 0) {
		req_p->result = 0x00040000;
		done(req_p);
		return 0;
	}

	if (done) {
		req_p->scsi_done = done;
	} else {
#ifdef ED_DBGP		
		printk( "atp870u_queuecommand: done can't be NULL\n");
#endif		
		req_p->result = 0;
		done(req_p);
		return 0;
	}
	
	/*
	 *	Count new command
	 */
	dev->quend[c]++;
	if (dev->quend[c] >= qcnt) {
		dev->quend[c] = 0;
	}
	
	/*
	 *	Check queue state
	 */
	if (dev->quhd[c] == dev->quend[c]) {
		if (dev->quend[c] == 0) {
			dev->quend[c] = qcnt;
		}
#ifdef ED_DBGP		
		printk("atp870u_queuecommand : dev->quhd[c] == dev->quend[c]\n");
#endif		
		dev->quend[c]--;
		req_p->result = 0x00020000;
		done(req_p);	
		return 0;
	}
	dev->quereq[c][dev->quend[c]] = req_p;
#ifdef ED_DBGP	
	printk("dev->ioport[c] = %x atp_readb_io(dev, c, 0x1c) = %x dev->in_int[%d] = %d dev->in_snd[%d] = %d\n",dev->ioport[c],atp_readb_io(dev, c, 0x1c),c,dev->in_int[c],c,dev->in_snd[c]);
#endif
	if ((atp_readb_io(dev, c, 0x1c) == 0) && (dev->in_int[c] == 0) && (dev->in_snd[c] == 0)) {
#ifdef ED_DBGP
		printk("Call sent_s870(atp870u_queuecommand)\n");
#endif		
		send_s870(dev,c);
	}
#ifdef ED_DBGP	
	printk("atp870u_queuecommand : exit\n");
#endif	
	return 0;
}

static DEF_SCSI_QCMD(atp870u_queuecommand)

/**
 *	send_s870	-	send a command to the controller
 *	@host: host
 *
 *	On entry there is work queued to be done. We move some of that work to the
 *	controller itself. 
 *
 *	Caller holds the host lock.
 */
static void send_s870(struct atp_unit *dev,unsigned char c)
{
	struct scsi_cmnd *workreq = NULL;
	unsigned int i;//,k;
	unsigned char  j, target_id;
	unsigned char *prd;
	unsigned short int w;
	unsigned long l, bttl = 0;
	unsigned long  sg_count;

	if (dev->in_snd[c] != 0) {
#ifdef ED_DBGP		
		printk("cmnd in_snd\n");
#endif
		return;
	}
#ifdef ED_DBGP
	printk("Sent_s870 enter\n");
#endif
	dev->in_snd[c] = 1;
	if ((dev->last_cmd[c] != 0xff) && ((dev->last_cmd[c] & 0x40) != 0)) {
		dev->last_cmd[c] &= 0x0f;
		workreq = dev->id[c][dev->last_cmd[c]].curr_req;
		if (!workreq) {
			dev->last_cmd[c] = 0xff;
			if (dev->quhd[c] == dev->quend[c]) {
				dev->in_snd[c] = 0;
				return;
			}
		}
	}
	if (!workreq) {
		if ((dev->last_cmd[c] != 0xff) && (dev->working[c] != 0)) {
			dev->in_snd[c] = 0;
			return;
		}
		dev->working[c]++;
		j = dev->quhd[c];
		dev->quhd[c]++;
		if (dev->quhd[c] >= qcnt)
			dev->quhd[c] = 0;
		workreq = dev->quereq[c][dev->quhd[c]];
		if (dev->id[c][scmd_id(workreq)].curr_req != NULL) {
			dev->quhd[c] = j;
			dev->working[c]--;
			dev->in_snd[c] = 0;
			return;
		}
		dev->id[c][scmd_id(workreq)].curr_req = workreq;
		dev->last_cmd[c] = scmd_id(workreq);
	}
	if ((atp_readb_io(dev, c, 0x1f) & 0xb0) != 0 || atp_readb_io(dev, c, 0x1c) != 0) {
#ifdef ED_DBGP
		printk("Abort to Send\n");
#endif
		dev->last_cmd[c] |= 0x40;
		dev->in_snd[c] = 0;
		return;
	}
#ifdef ED_DBGP
	printk("OK to Send\n");
	scmd_printk(KERN_DEBUG, workreq, "CDB");
	for(i=0;i<workreq->cmd_len;i++) {
		printk(" %x",workreq->cmnd[i]);
	}
	printk("\n");
#endif	
	l = scsi_bufflen(workreq);

	if (is885(dev)) {
		j = atp_readb_base(dev, 0x29) & 0xfe;
		atp_writeb_base(dev, 0x29, j);
		dev->r1f[c][scmd_id(workreq)] = 0;
	}
	
	if (workreq->cmnd[0] == READ_CAPACITY) {
		if (l > 8)
			l = 8;
	}
	if (workreq->cmnd[0] == 0x00) {
		l = 0;
	}

	j = 0;
	target_id = scmd_id(workreq);

	/*
	 *	Wide ?
	 */
	w = 1;
	w = w << target_id;
	if ((w & dev->wide_id[c]) != 0) {
		j |= 0x01;
	}
	atp_writeb_io(dev, c, 0x1b, j);
	while ((atp_readb_io(dev, c, 0x1b) & 0x01) != j) {
		atp_writeb_pci(dev, c, 0x1b, j);
#ifdef ED_DBGP
		printk("send_s870 while loop 1\n");
#endif
	}
	/*
	 *	Write the command
	 */

	atp_writeb_io(dev, c, 0x00, workreq->cmd_len);
	atp_writeb_io(dev, c, 0x01, 0x2c);
	if (is885(dev))
		atp_writeb_io(dev, c, 0x02, 0x7f);
	else
		atp_writeb_io(dev, c, 0x02, 0xcf);
	for (i = 0; i < workreq->cmd_len; i++)
		atp_writeb_io(dev, c, 0x03 + i, workreq->cmnd[i]);
	atp_writeb_io(dev, c, 0x0f, workreq->device->lun);
	/*
	 *	Write the target
	 */
	atp_writeb_io(dev, c, 0x11, dev->id[c][target_id].devsp);
#ifdef ED_DBGP	
	printk("dev->id[%d][%d].devsp = %2x\n",c,target_id,dev->id[c][target_id].devsp);
#endif

	sg_count = scsi_dma_map(workreq);
	/*
	 *	Write transfer size
	 */
	atp_writeb_io(dev, c, 0x12, ((unsigned char *) (&l))[2]);
	atp_writeb_io(dev, c, 0x13, ((unsigned char *) (&l))[1]);
	atp_writeb_io(dev, c, 0x14, ((unsigned char *) (&l))[0]);
	j = target_id;	
	dev->id[c][j].last_len = l;
	dev->id[c][j].tran_len = 0;
#ifdef ED_DBGP	
	printk("dev->id[%2d][%2d].last_len = %d\n",c,j,dev->id[c][j].last_len);
#endif	
	/*
	 *	Flip the wide bits
	 */
	if ((j & 0x08) != 0) {
		j = (j & 0x07) | 0x40;
	}
	/*
	 *	Check transfer direction
	 */
	if (workreq->sc_data_direction == DMA_TO_DEVICE)
		atp_writeb_io(dev, c, 0x15, j | 0x20);
	else
		atp_writeb_io(dev, c, 0x15, j);
	atp_writeb_io(dev, c, 0x16, atp_readb_io(dev, c, 0x16) | 0x80);
	atp_writeb_io(dev, c, 0x16, 0x80);
	dev->id[c][target_id].dirct = 0;
	if (l == 0) {
		if (atp_readb_io(dev, c, 0x1c) == 0) {
#ifdef ED_DBGP
			printk("change SCSI_CMD_REG 0x08\n");	
#endif				
			atp_writeb_io(dev, c, 0x18, 0x08);
		} else
			dev->last_cmd[c] |= 0x40;
		dev->in_snd[c] = 0;
		return;
	}
	prd = dev->id[c][target_id].prd_table;
	dev->id[c][target_id].prd_pos = prd;

	/*
	 *	Now write the request list. Either as scatter/gather or as
	 *	a linear chain.
	 */

	if (l) {
		struct scatterlist *sgpnt;
		i = 0;
		scsi_for_each_sg(workreq, sgpnt, sg_count, j) {
			bttl = sg_dma_address(sgpnt);
			l=sg_dma_len(sgpnt);
#ifdef ED_DBGP		
			printk("1. bttl %x, l %x\n",bttl, l);
#endif			
			while (l > 0x10000) {
				(((u16 *) (prd))[i + 3]) = 0x0000;
				(((u16 *) (prd))[i + 2]) = 0x0000;
				(((u32 *) (prd))[i >> 1]) = cpu_to_le32(bttl);
				l -= 0x10000;
				bttl += 0x10000;
				i += 0x04;
			}
			(((u32 *) (prd))[i >> 1]) = cpu_to_le32(bttl);
			(((u16 *) (prd))[i + 2]) = cpu_to_le16(l);
			(((u16 *) (prd))[i + 3]) = 0;
			i += 0x04;			
		}
		(((u16 *) (prd))[i - 1]) = cpu_to_le16(0x8000);	
#ifdef ED_DBGP		
		printk("prd %4x %4x %4x %4x\n",(((unsigned short int *)prd)[0]),(((unsigned short int *)prd)[1]),(((unsigned short int *)prd)[2]),(((unsigned short int *)prd)[3]));
		printk("2. bttl %x, l %x\n",bttl, l);
#endif			
	}
#ifdef ED_DBGP		
	printk("send_s870: prdaddr_2 0x%8x target_id %d\n", dev->id[c][target_id].prdaddr,target_id);
#endif	
	dev->id[c][target_id].prdaddr = dev->id[c][target_id].prd_bus;
	atp_writel_pci(dev, c, 4, dev->id[c][target_id].prdaddr);
	atp_writeb_pci(dev, c, 2, 0x06);
	atp_writeb_pci(dev, c, 2, 0x00);
	if (is885(dev)) {
		j = atp_readb_pci(dev, c, 1) & 0xf3;
		if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) ||
	    	(workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a)) {
	   		j |= 0x0c;
		}
		atp_writeb_pci(dev, c, 1, j);
	} else if (is880(dev)) {
		if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) || (workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a))
			atp_writeb_base(dev, 0x3b, (atp_readb_base(dev, 0x3b) & 0x3f) | 0xc0);
		else
			atp_writeb_base(dev, 0x3b, atp_readb_base(dev, 0x3b) & 0x3f);
	} else {		
		if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) || (workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a))
			atp_writeb_base(dev, 0x3a, (atp_readb_base(dev, 0x3a) & 0xf3) | 0x08);
		else
			atp_writeb_base(dev, 0x3a, atp_readb_base(dev, 0x3a) & 0xf3);
	}	

	if(workreq->sc_data_direction == DMA_TO_DEVICE) {
		dev->id[c][target_id].dirct = 0x20;
		if (atp_readb_io(dev, c, 0x1c) == 0) {
			atp_writeb_io(dev, c, 0x18, 0x08);
			atp_writeb_pci(dev, c, 0, 0x01);
#ifdef ED_DBGP		
		printk( "start DMA(to target)\n");
#endif				
		} else {
			dev->last_cmd[c] |= 0x40;
		}
		dev->in_snd[c] = 0;
		return;
	}
	if (atp_readb_io(dev, c, 0x1c) == 0) {
		atp_writeb_io(dev, c, 0x18, 0x08);
		atp_writeb_pci(dev, c, 0, 0x09);
#ifdef ED_DBGP		
		printk( "start DMA(to host)\n");
#endif			
	} else {
		dev->last_cmd[c] |= 0x40;
	}
	dev->in_snd[c] = 0;
	return;

}

static unsigned char fun_scam(struct atp_unit *dev, unsigned short int *val)
{
	unsigned short int i, k;
	unsigned char j;

	atp_writew_io(dev, 0, 0x1c, *val);
	for (i = 0; i < 10; i++) {	/* stable >= bus settle delay(400 ns)  */
		k = atp_readw_io(dev, 0, 0x1c);
		j = (unsigned char) (k >> 8);
		if ((k & 0x8000) != 0)	/* DB7 all release?    */
			i = 0;
	}
	*val |= 0x4000;		/* assert DB6           */
	atp_writew_io(dev, 0, 0x1c, *val);
	*val &= 0xdfff;		/* assert DB5           */
	atp_writew_io(dev, 0, 0x1c, *val);
	for (i = 0; i < 10; i++) {	/* stable >= bus settle delay(400 ns) */
		if ((atp_readw_io(dev, 0, 0x1c) & 0x2000) != 0)	/* DB5 all release?       */
			i = 0;
	}
	*val |= 0x8000;		/* no DB4-0, assert DB7    */
	*val &= 0xe0ff;
	atp_writew_io(dev, 0, 0x1c, *val);
	*val &= 0xbfff;		/* release DB6             */
	atp_writew_io(dev, 0, 0x1c, *val);
	for (i = 0; i < 10; i++) {	/* stable >= bus settle delay(400 ns)  */
		if ((atp_readw_io(dev, 0, 0x1c) & 0x4000) != 0)	/* DB6 all release?  */
			i = 0;
	}

	return j;
}

static void tscam(struct Scsi_Host *host, bool wide_chip, u8 scam_on)
{

	unsigned char i, j, k;
	unsigned long n;
	unsigned short int m, assignid_map, val;
	unsigned char mbuf[33], quintet[2];
	struct atp_unit *dev = (struct atp_unit *)&host->hostdata;
	static unsigned char g2q_tab[8] = {
		0x38, 0x31, 0x32, 0x2b, 0x34, 0x2d, 0x2e, 0x27
	};

/*  I can't believe we need this before we've even done anything.  Remove it
 *  and see if anyone bitches.
	for (i = 0; i < 0x10; i++) {
		udelay(0xffff);
	}
 */

	atp_writeb_io(dev, 0, 1, 0x08);
	atp_writeb_io(dev, 0, 2, 0x7f);
	atp_writeb_io(dev, 0, 0x11, 0x20);

	if ((scam_on & 0x40) == 0) {
		return;
	}
	m = 1;
	m <<= dev->host_id[0];
	j = 16;
	if (!wide_chip) {
		m |= 0xff00;
		j = 8;
	}
	assignid_map = m;
	atp_writeb_io(dev, 0, 0x02, 0x02);	/* 2*2=4ms,3EH 2/32*3E=3.9ms */
	atp_writeb_io(dev, 0, 0x03, 0);
	atp_writeb_io(dev, 0, 0x04, 0);
	atp_writeb_io(dev, 0, 0x05, 0);
	atp_writeb_io(dev, 0, 0x06, 0);
	atp_writeb_io(dev, 0, 0x07, 0);
	atp_writeb_io(dev, 0, 0x08, 0);

	for (i = 0; i < j; i++) {
		m = 1;
		m = m << i;
		if ((m & assignid_map) != 0) {
			continue;
		}
		atp_writeb_io(dev, 0, 0x0f, 0);
		atp_writeb_io(dev, 0, 0x12, 0);
		atp_writeb_io(dev, 0, 0x13, 0);
		atp_writeb_io(dev, 0, 0x14, 0);
		if (i > 7) {
			k = (i & 0x07) | 0x40;
		} else {
			k = i;
		}
		atp_writeb_io(dev, 0, 0x15, k);
		if (wide_chip)
			atp_writeb_io(dev, 0, 0x1b, 0x01);
		else
			atp_writeb_io(dev, 0, 0x1b, 0x00);
		do {
			atp_writeb_io(dev, 0, 0x18, 0x09);

			while ((atp_readb_io(dev, 0, 0x1f) & 0x80) == 0x00)
				cpu_relax();
			k = atp_readb_io(dev, 0, 0x17);
			if ((k == 0x85) || (k == 0x42))
				break;
			if (k != 0x16)
				atp_writeb_io(dev, 0, 0x10, 0x41);
		} while (k != 0x16);
		if ((k == 0x85) || (k == 0x42))
			continue;
		assignid_map |= m;

	}
	atp_writeb_io(dev, 0, 0x02, 0x7f);
	atp_writeb_io(dev, 0, 0x1b, 0x02);

	udelay(2);

	val = 0x0080;		/* bsy  */
	atp_writew_io(dev, 0, 0x1c, val);
	val |= 0x0040;		/* sel  */
	atp_writew_io(dev, 0, 0x1c, val);
	val |= 0x0004;		/* msg  */
	atp_writew_io(dev, 0, 0x1c, val);
	udelay(2);		/* 2 deskew delay(45ns*2=90ns) */
	val &= 0x007f;		/* no bsy  */
	atp_writew_io(dev, 0, 0x1c, val);
	msleep(128);
	val &= 0x00fb;		/* after 1ms no msg */
	atp_writew_io(dev, 0, 0x1c, val);
	while ((atp_readb_io(dev, 0, 0x1c) & 0x04) != 0)
		;
	udelay(2);
	udelay(100);
	for (n = 0; n < 0x30000; n++)
		if ((atp_readb_io(dev, 0, 0x1c) & 0x80) != 0)	/* bsy ? */
			break;
	if (n < 0x30000)
		for (n = 0; n < 0x30000; n++)
			if ((atp_readb_io(dev, 0, 0x1c) & 0x81) == 0x0081) {
				udelay(2);
				val |= 0x8003;		/* io,cd,db7  */
				atp_writew_io(dev, 0, 0x1c, val);
				udelay(2);
				val &= 0x00bf;		/* no sel     */
				atp_writew_io(dev, 0, 0x1c, val);
				udelay(2);
				break;
			}
	while (1) {
	/*
	 * The funny division into multiple delays is to accomodate
	 * arches like ARM where udelay() multiplies its argument by
	 * a large number to initialize a loop counter.  To avoid
	 * overflow, the maximum supported udelay is 2000 microseconds.
	 *
	 * XXX it would be more polite to find a way to use msleep()
	 */
	mdelay(2);
	udelay(48);
	if ((atp_readb_io(dev, 0, 0x1c) & 0x80) == 0x00) {	/* bsy ? */
		atp_writew_io(dev, 0, 0x1c, 0);
		atp_writeb_io(dev, 0, 0x1b, 0);
		atp_writeb_io(dev, 0, 0x15, 0);
		atp_writeb_io(dev, 0, 0x18, 0x09);
		while ((atp_readb_io(dev, 0, 0x1f) & 0x80) == 0)
			cpu_relax();
		atp_readb_io(dev, 0, 0x17);
		return;
	}
	val &= 0x00ff;		/* synchronization  */
	val |= 0x3f00;
	fun_scam(dev, &val);
	udelay(2);
	val &= 0x00ff;		/* isolation        */
	val |= 0x2000;
	fun_scam(dev, &val);
	udelay(2);
	i = 8;
	j = 0;

	while (1) {
		if ((atp_readw_io(dev, 0, 0x1c) & 0x2000) == 0)
			continue;
		udelay(2);
		val &= 0x00ff;		/* get ID_STRING */
		val |= 0x2000;
		k = fun_scam(dev, &val);
		if ((k & 0x03) == 0)
			break;
		mbuf[j] <<= 0x01;
		mbuf[j] &= 0xfe;
		if ((k & 0x02) != 0)
			mbuf[j] |= 0x01;
		i--;
		if (i > 0)
			continue;
		j++;
		i = 8;
	}

	/* isolation complete..  */
/*    mbuf[32]=0;
	printk(" \n%x %x %x %s\n ",assignid_map,mbuf[0],mbuf[1],&mbuf[2]); */
	i = 15;
	j = mbuf[0];
	if ((j & 0x20) != 0) {	/* bit5=1:ID up to 7      */
		i = 7;
	}
	if ((j & 0x06) != 0) {	/* IDvalid?             */
		k = mbuf[1];
		while (1) {
			m = 1;
			m <<= k;
			if ((m & assignid_map) == 0)
				break;
			if (k > 0)
				k--;
			else
				break;
		}
	}
	if ((m & assignid_map) != 0) {	/* srch from max acceptable ID#  */
		k = i;			/* max acceptable ID#            */
		while (1) {
			m = 1;
			m <<= k;
			if ((m & assignid_map) == 0)
				break;
			if (k > 0)
				k--;
			else
				break;
		}
	}
	/* k=binID#,       */
	assignid_map |= m;
	if (k < 8) {
		quintet[0] = 0x38;	/* 1st dft ID<8    */
	} else {
		quintet[0] = 0x31;	/* 1st  ID>=8      */
	}
	k &= 0x07;
	quintet[1] = g2q_tab[k];

	val &= 0x00ff;		/* AssignID 1stQuintet,AH=001xxxxx  */
	m = quintet[0] << 8;
	val |= m;
	fun_scam(dev, &val);
	val &= 0x00ff;		/* AssignID 2ndQuintet,AH=001xxxxx */
	m = quintet[1] << 8;
	val |= m;
	fun_scam(dev, &val);

	}
}

static void atp870u_free_tables(struct Scsi_Host *host)
{
	struct atp_unit *atp_dev = (struct atp_unit *)&host->hostdata;
	int j, k;
	for (j=0; j < 2; j++) {
		for (k = 0; k < 16; k++) {
			if (!atp_dev->id[j][k].prd_table)
				continue;
			dma_free_coherent(&atp_dev->pdev->dev, 1024, atp_dev->id[j][k].prd_table, atp_dev->id[j][k].prd_bus);
			atp_dev->id[j][k].prd_table = NULL;
		}
	}
}

static int atp870u_init_tables(struct Scsi_Host *host)
{
	struct atp_unit *atp_dev = (struct atp_unit *)&host->hostdata;
	int c,k;
	for(c=0;c < 2;c++) {
	   	for(k=0;k<16;k++) {
				atp_dev->id[c][k].prd_table = dma_alloc_coherent(&atp_dev->pdev->dev, 1024, &(atp_dev->id[c][k].prd_bus), GFP_KERNEL);
	   			if (!atp_dev->id[c][k].prd_table) {
	   				printk("atp870u_init_tables fail\n");
				atp870u_free_tables(host);
				return -ENOMEM;
			}
			atp_dev->id[c][k].prdaddr = atp_dev->id[c][k].prd_bus;
			atp_dev->id[c][k].devsp=0x20;
			atp_dev->id[c][k].devtype = 0x7f;
			atp_dev->id[c][k].curr_req = NULL;			   
	   	}
	   			
	   	atp_dev->active_id[c] = 0;
	   	atp_dev->wide_id[c] = 0;
	   	atp_dev->host_id[c] = 0x07;
	   	atp_dev->quhd[c] = 0;
	   	atp_dev->quend[c] = 0;
	   	atp_dev->last_cmd[c] = 0xff;
	   	atp_dev->in_snd[c] = 0;
	   	atp_dev->in_int[c] = 0;
	   	
	   	for (k = 0; k < qcnt; k++) {
	   		  atp_dev->quereq[c][k] = NULL;
	   	}	   		   
	   	for (k = 0; k < 16; k++) {
			   atp_dev->id[c][k].curr_req = NULL;
			   atp_dev->sp[c][k] = 0x04;
	   	}		   
	}
	return 0;
}

static void atp_set_host_id(struct atp_unit *atp, u8 c, u8 host_id)
{
	atp_writeb_io(atp, c, 0, host_id | 0x08);
	atp_writeb_io(atp, c, 0x18, 0);
	while ((atp_readb_io(atp, c, 0x1f) & 0x80) == 0)
		mdelay(1);
	atp_readb_io(atp, c, 0x17);
	atp_writeb_io(atp, c, 1, 8);
	atp_writeb_io(atp, c, 2, 0x7f);
	atp_writeb_io(atp, c, 0x11, 0x20);
}

static void atp870_init(struct Scsi_Host *shpnt)
{
	struct atp_unit *atpdev = shost_priv(shpnt);
	struct pci_dev *pdev = atpdev->pdev;
	unsigned char k, host_id;
	u8 scam_on;
	bool wide_chip =
		(pdev->device == PCI_DEVICE_ID_ARTOP_AEC7610 &&
		 pdev->revision == 4) ||
		(pdev->device == PCI_DEVICE_ID_ARTOP_AEC7612UW) ||
		(pdev->device == PCI_DEVICE_ID_ARTOP_AEC7612SUW);

	pci_read_config_byte(pdev, 0x49, &host_id);

	dev_info(&pdev->dev, "ACARD AEC-671X PCI Ultra/W SCSI-2/3 Host Adapter: IO:%lx, IRQ:%d.\n",
		 shpnt->io_port, shpnt->irq);

	atpdev->ioport[0] = shpnt->io_port;
	atpdev->pciport[0] = shpnt->io_port + 0x20;
	host_id &= 0x07;
	atpdev->host_id[0] = host_id;
	scam_on = atp_readb_pci(atpdev, 0, 2);
	atpdev->global_map[0] = atp_readb_base(atpdev, 0x2d);
	atpdev->ultra_map[0] = atp_readw_base(atpdev, 0x2e);

	if (atpdev->ultra_map[0] == 0) {
		scam_on = 0x00;
		atpdev->global_map[0] = 0x20;
		atpdev->ultra_map[0] = 0xffff;
	}

	if (pdev->revision > 0x07)	/* check if atp876 chip */
		atp_writeb_base(atpdev, 0x3e, 0x00); /* enable terminator */

	k = (atp_readb_base(atpdev, 0x3a) & 0xf3) | 0x10;
	atp_writeb_base(atpdev, 0x3a, k);
	atp_writeb_base(atpdev, 0x3a, k & 0xdf);
	msleep(32);
	atp_writeb_base(atpdev, 0x3a, k);
	msleep(32);
	atp_set_host_id(atpdev, 0, host_id);

	tscam(shpnt, wide_chip, scam_on);
	atp_writeb_base(atpdev, 0x3a, atp_readb_base(atpdev, 0x3a) | 0x10);
	atp_is(atpdev, 0, wide_chip, 0);
	atp_writeb_base(atpdev, 0x3a, atp_readb_base(atpdev, 0x3a) & 0xef);
	atp_writeb_base(atpdev, 0x3b, atp_readb_base(atpdev, 0x3b) | 0x20);
	shpnt->max_id = wide_chip ? 16 : 8;
	shpnt->this_id = host_id;
}

static void atp880_init(struct Scsi_Host *shpnt)
{
	struct atp_unit *atpdev = shost_priv(shpnt);
	struct pci_dev *pdev = atpdev->pdev;
	unsigned char k, m, host_id;
	unsigned int n;

	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x80);

	atpdev->ioport[0] = shpnt->io_port + 0x40;
	atpdev->pciport[0] = shpnt->io_port + 0x28;

	host_id = atp_readb_base(atpdev, 0x39) >> 4;

	dev_info(&pdev->dev, "ACARD AEC-67160 PCI Ultra3 LVD Host Adapter: IO:%lx, IRQ:%d.\n",
		 shpnt->io_port, shpnt->irq);
	atpdev->host_id[0] = host_id;

	atpdev->global_map[0] = atp_readb_base(atpdev, 0x35);
	atpdev->ultra_map[0] = atp_readw_base(atpdev, 0x3c);

	n = 0x3f09;
	while (n < 0x4000) {
		m = 0;
		atp_writew_base(atpdev, 0x34, n);
		n += 0x0002;
		if (atp_readb_base(atpdev, 0x30) == 0xff)
			break;

		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x30);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x31);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x32);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x33);
		atp_writew_base(atpdev, 0x34, n);
		n += 0x0002;
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x30);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x31);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x32);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x33);
		atp_writew_base(atpdev, 0x34, n);
		n += 0x0002;
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x30);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x31);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x32);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x33);
		atp_writew_base(atpdev, 0x34, n);
		n += 0x0002;
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x30);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x31);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x32);
		atpdev->sp[0][m++] = atp_readb_base(atpdev, 0x33);
		n += 0x0018;
	}
	atp_writew_base(atpdev, 0x34, 0);
	atpdev->ultra_map[0] = 0;
	atpdev->async[0] = 0;
	for (k = 0; k < 16; k++) {
		n = 1 << k;
		if (atpdev->sp[0][k] > 1)
			atpdev->ultra_map[0] |= n;
		else
			if (atpdev->sp[0][k] == 0)
				atpdev->async[0] |= n;
	}
	atpdev->async[0] = ~(atpdev->async[0]);
	atp_writeb_base(atpdev, 0x35, atpdev->global_map[0]);

	k = atp_readb_base(atpdev, 0x38) & 0x80;
	atp_writeb_base(atpdev, 0x38, k);
	atp_writeb_base(atpdev, 0x3b, 0x20);
	msleep(32);
	atp_writeb_base(atpdev, 0x3b, 0);
	msleep(32);
	atp_readb_io(atpdev, 0, 0x1b);
	atp_readb_io(atpdev, 0, 0x17);

	atp_set_host_id(atpdev, 0, host_id);

	tscam(shpnt, true, atp_readb_base(atpdev, 0x22));
	atp_is(atpdev, 0, true, atp_readb_base(atpdev, 0x3f) & 0x40);
	atp_writeb_base(atpdev, 0x38, 0xb0);
	shpnt->max_id = 16;
	shpnt->this_id = host_id;
}

static void atp885_init(struct Scsi_Host *shpnt)
{
	struct atp_unit *atpdev = shost_priv(shpnt);
	struct pci_dev *pdev = atpdev->pdev;
	unsigned char k, m, c;
	unsigned int n;
	unsigned char setupdata[2][16];

	dev_info(&pdev->dev, "ACARD AEC-67162 PCI Ultra3 LVD Host Adapter: IO:%lx, IRQ:%d.\n",
		 shpnt->io_port, shpnt->irq);

	atpdev->ioport[0] = shpnt->io_port + 0x80;
	atpdev->ioport[1] = shpnt->io_port + 0xc0;
	atpdev->pciport[0] = shpnt->io_port + 0x40;
	atpdev->pciport[1] = shpnt->io_port + 0x50;

	c = atp_readb_base(atpdev, 0x29);
	atp_writeb_base(atpdev, 0x29, c | 0x04);

	n = 0x1f80;
	while (n < 0x2000) {
		atp_writew_base(atpdev, 0x3c, n);
		if (atp_readl_base(atpdev, 0x38) == 0xffffffff)
			break;
		for (m = 0; m < 2; m++) {
			atpdev->global_map[m] = 0;
			for (k = 0; k < 4; k++) {
				atp_writew_base(atpdev, 0x3c, n++);
				((u32 *)&setupdata[m][0])[k] = atp_readl_base(atpdev, 0x38);
			}
			for (k = 0; k < 4; k++) {
				atp_writew_base(atpdev, 0x3c, n++);
				((u32 *)&atpdev->sp[m][0])[k] = atp_readl_base(atpdev, 0x38);
			}
			n += 8;
		}
	}
	c = atp_readb_base(atpdev, 0x29);
	atp_writeb_base(atpdev, 0x29, c & 0xfb);
	for (c = 0; c < 2; c++) {
		atpdev->ultra_map[c] = 0;
		atpdev->async[c] = 0;
		for (k = 0; k < 16; k++) {
			n = 1 << k;
			if (atpdev->sp[c][k] > 1)
				atpdev->ultra_map[c] |= n;
			else
				if (atpdev->sp[c][k] == 0)
					atpdev->async[c] |= n;
		}
		atpdev->async[c] = ~(atpdev->async[c]);

		if (atpdev->global_map[c] == 0) {
			k = setupdata[c][1];
			if ((k & 0x40) != 0)
				atpdev->global_map[c] |= 0x20;
			k &= 0x07;
			atpdev->global_map[c] |= k;
			if ((setupdata[c][2] & 0x04) != 0)
				atpdev->global_map[c] |= 0x08;
			atpdev->host_id[c] = setupdata[c][0] & 0x07;
		}
	}

	k = atp_readb_base(atpdev, 0x28) & 0x8f;
	k |= 0x10;
	atp_writeb_base(atpdev, 0x28, k);
	atp_writeb_pci(atpdev, 0, 1, 0x80);
	atp_writeb_pci(atpdev, 1, 1, 0x80);
	msleep(100);
	atp_writeb_pci(atpdev, 0, 1, 0);
	atp_writeb_pci(atpdev, 1, 1, 0);
	msleep(1000);
	atp_readb_io(atpdev, 0, 0x1b);
	atp_readb_io(atpdev, 0, 0x17);
	atp_readb_io(atpdev, 1, 0x1b);
	atp_readb_io(atpdev, 1, 0x17);

	k = atpdev->host_id[0];
	if (k > 7)
		k = (k & 0x07) | 0x40;
	atp_set_host_id(atpdev, 0, k);

	k = atpdev->host_id[1];
	if (k > 7)
		k = (k & 0x07) | 0x40;
	atp_set_host_id(atpdev, 1, k);

	msleep(600); /* this delay used to be called tscam_885() */
	dev_info(&pdev->dev, "Scanning Channel A SCSI Device ...\n");
	atp_is(atpdev, 0, true, atp_readb_io(atpdev, 0, 0x1b) >> 7);
	atp_writeb_io(atpdev, 0, 0x16, 0x80);
	dev_info(&pdev->dev, "Scanning Channel B SCSI Device ...\n");
	atp_is(atpdev, 1, true, atp_readb_io(atpdev, 1, 0x1b) >> 7);
	atp_writeb_io(atpdev, 1, 0x16, 0x80);
	k = atp_readb_base(atpdev, 0x28) & 0xcf;
	k |= 0xc0;
	atp_writeb_base(atpdev, 0x28, k);
	k = atp_readb_base(atpdev, 0x1f) | 0x80;
	atp_writeb_base(atpdev, 0x1f, k);
	k = atp_readb_base(atpdev, 0x29) | 0x01;
	atp_writeb_base(atpdev, 0x29, k);
	shpnt->max_id = 16;
	shpnt->max_lun = (atpdev->global_map[0] & 0x07) + 1;
	shpnt->max_channel = 1;
	shpnt->this_id = atpdev->host_id[0];
}

/* return non-zero on detection */
static int atp870u_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct Scsi_Host *shpnt = NULL;
	struct atp_unit *atpdev;
	int err;

	if (ent->device == PCI_DEVICE_ID_ARTOP_AEC7610 && pdev->revision < 2) {
		dev_err(&pdev->dev, "ATP850S chips (AEC6710L/F cards) are not supported.\n");
		return -ENODEV;
	}

	err = pci_enable_device(pdev);
	if (err)
		goto fail;

	if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
                printk(KERN_ERR "atp870u: DMA mask required but not available.\n");
                err = -EIO;
                goto disable_device;
        }

	err = pci_request_regions(pdev, "atp870u");
	if (err)
		goto disable_device;
	pci_set_master(pdev);

        err = -ENOMEM;
	shpnt = scsi_host_alloc(&atp870u_template, sizeof(struct atp_unit));
	if (!shpnt)
		goto release_region;

	atpdev = shost_priv(shpnt);

	atpdev->host = shpnt;
	atpdev->pdev = pdev;
	pci_set_drvdata(pdev, atpdev);

	shpnt->io_port = pci_resource_start(pdev, 0);
	shpnt->io_port &= 0xfffffff8;
	shpnt->n_io_port = pci_resource_len(pdev, 0);
	atpdev->baseport = shpnt->io_port;
	shpnt->unique_id = shpnt->io_port;
	shpnt->irq = pdev->irq;

	err = atp870u_init_tables(shpnt);
	if (err) {
		dev_err(&pdev->dev, "Unable to allocate tables for Acard controller\n");
		goto unregister;
	}

	if (is880(atpdev))
		atp880_init(shpnt);
	else if (is885(atpdev))
		atp885_init(shpnt);
	else
		atp870_init(shpnt);

	err = request_irq(shpnt->irq, atp870u_intr_handle, IRQF_SHARED, "atp870u", shpnt);
	if (err) {
		dev_err(&pdev->dev, "Unable to allocate IRQ %d.\n", shpnt->irq);
		goto free_tables;
	}

	err = scsi_add_host(shpnt, &pdev->dev);
	if (err)
		goto scsi_add_fail;
	scsi_scan_host(shpnt);

	return 0;

scsi_add_fail:
	free_irq(shpnt->irq, shpnt);
free_tables:
	atp870u_free_tables(shpnt);
unregister:
	scsi_host_put(shpnt);
release_region:
	pci_release_regions(pdev);
disable_device:
	pci_disable_device(pdev);
fail:
	return err;
}

/* The abort command does not leave the device in a clean state where
   it is available to be used again.  Until this gets worked out, we will
   leave it commented out.  */

static int atp870u_abort(struct scsi_cmnd * SCpnt)
{
	unsigned char  j, k, c;
	struct scsi_cmnd *workrequ;
	struct atp_unit *dev;	
	struct Scsi_Host *host;
	host = SCpnt->device->host;

	dev = (struct atp_unit *)&host->hostdata;
	c = scmd_channel(SCpnt);
	printk(" atp870u: abort Channel = %x \n", c);
	printk("working=%x last_cmd=%x ", dev->working[c], dev->last_cmd[c]);
	printk(" quhdu=%x quendu=%x ", dev->quhd[c], dev->quend[c]);
	for (j = 0; j < 0x18; j++) {
		printk(" r%2x=%2x", j, atp_readb_io(dev, c, j));
	}
	printk(" r1c=%2x", atp_readb_io(dev, c, 0x1c));
	printk(" r1f=%2x in_snd=%2x ", atp_readb_io(dev, c, 0x1f), dev->in_snd[c]);
	printk(" d00=%2x", atp_readb_pci(dev, c, 0x00));
	printk(" d02=%2x", atp_readb_pci(dev, c, 0x02));
	for(j=0;j<16;j++) {
	   if (dev->id[c][j].curr_req != NULL) {
		workrequ = dev->id[c][j].curr_req;
		printk("\n que cdb= ");
		for (k=0; k < workrequ->cmd_len; k++) {
		    printk(" %2x ",workrequ->cmnd[k]);
		}
		printk(" last_lenu= %x ",(unsigned int)dev->id[c][j].last_len);
	   }
	}
	return SUCCESS;
}

static const char *atp870u_info(struct Scsi_Host *notused)
{
	static char buffer[128];

	strcpy(buffer, "ACARD AEC-6710/6712/67160 PCI Ultra/W/LVD SCSI-3 Adapter Driver V2.6+ac ");

	return buffer;
}

static int atp870u_show_info(struct seq_file *m, struct Scsi_Host *HBAptr)
{
	seq_puts(m, "ACARD AEC-671X Driver Version: 2.6+ac\n\n"
		"Adapter Configuration:\n");
	seq_printf(m, "               Base IO: %#.4lx\n", HBAptr->io_port);
	seq_printf(m, "                   IRQ: %d\n", HBAptr->irq);
	return 0;
}


static int atp870u_biosparam(struct scsi_device *disk, struct block_device *dev,
			sector_t capacity, int *ip)
{
	int heads, sectors, cylinders;

	heads = 64;
	sectors = 32;
	cylinders = (unsigned long)capacity / (heads * sectors);
	if (cylinders > 1024) {
		heads = 255;
		sectors = 63;
		cylinders = (unsigned long)capacity / (heads * sectors);
	}
	ip[0] = heads;
	ip[1] = sectors;
	ip[2] = cylinders;

	return 0;
}

static void atp870u_remove (struct pci_dev *pdev)
{	
	struct atp_unit *devext = pci_get_drvdata(pdev);
	struct Scsi_Host *pshost = devext->host;
	
	
	scsi_remove_host(pshost);
	free_irq(pshost->irq, pshost);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	atp870u_free_tables(pshost);
	scsi_host_put(pshost);
}
MODULE_LICENSE("GPL");

static struct scsi_host_template atp870u_template = {
     .module			= THIS_MODULE,
     .name              	= "atp870u"		/* name */,
     .proc_name			= "atp870u",
     .show_info			= atp870u_show_info,
     .info              	= atp870u_info		/* info */,
     .queuecommand      	= atp870u_queuecommand	/* queuecommand */,
     .eh_abort_handler  	= atp870u_abort		/* abort */,
     .bios_param        	= atp870u_biosparam	/* biosparm */,
     .can_queue         	= qcnt			/* can_queue */,
     .this_id           	= 7			/* SCSI ID */,
     .sg_tablesize      	= ATP870U_SCATTER	/*SG_ALL*/ /*SG_NONE*/,
     .max_sectors		= ATP870U_MAX_SECTORS,
};

static struct pci_device_id atp870u_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ARTOP, ATP885_DEVID)			  },
	{ PCI_DEVICE(PCI_VENDOR_ID_ARTOP, ATP880_DEVID1)			  },
	{ PCI_DEVICE(PCI_VENDOR_ID_ARTOP, ATP880_DEVID2)			  },
	{ PCI_DEVICE(PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_AEC7610)    },
	{ PCI_DEVICE(PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_AEC7612UW)  },
	{ PCI_DEVICE(PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_AEC7612U)   },
	{ PCI_DEVICE(PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_AEC7612S)   },
	{ PCI_DEVICE(PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_AEC7612D)	  },
	{ PCI_DEVICE(PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_AEC7612SUW) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ARTOP, PCI_DEVICE_ID_ARTOP_8060)	  },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, atp870u_id_table);

static struct pci_driver atp870u_driver = {
	.id_table	= atp870u_id_table,
	.name		= "atp870u",
	.probe		= atp870u_probe,
	.remove		= atp870u_remove,
};

module_pci_driver(atp870u_driver);

static void atp_is(struct atp_unit *dev, unsigned char c, bool wide_chip, unsigned char lvdmode)
{
	unsigned char i, j, k, rmb, n;
	unsigned short int m;
	static unsigned char mbuf[512];
	static unsigned char satn[9] = { 0, 0, 0, 0, 0, 0, 0, 6, 6 };
	static unsigned char inqd[9] = { 0x12, 0, 0, 0, 0x24, 0, 0, 0x24, 6 };
	static unsigned char synn[6] = { 0x80, 1, 3, 1, 0x19, 0x0e };
	unsigned char synu[6] = { 0x80, 1, 3, 1, 0x0a, 0x0e };
	static unsigned char synw[6] = { 0x80, 1, 3, 1, 0x19, 0x0e };
	static unsigned char synw_870[6] = { 0x80, 1, 3, 1, 0x0c, 0x07 };
	unsigned char synuw[6] = { 0x80, 1, 3, 1, 0x0a, 0x0e };
	static unsigned char wide[6] = { 0x80, 1, 2, 3, 1, 0 };
	static unsigned char u3[9] = { 0x80, 1, 6, 4, 0x09, 00, 0x0e, 0x01, 0x02 };

	for (i = 0; i < 16; i++) {
		if (!wide_chip && (i > 7))
			break;
		m = 1;
		m = m << i;
		if ((m & dev->active_id[c]) != 0) {
			continue;
		}
		if (i == dev->host_id[c]) {
			printk(KERN_INFO "         ID: %2d  Host Adapter\n", dev->host_id[c]);
			continue;
		}
		atp_writeb_io(dev, c, 0x1b, wide_chip ? 0x01 : 0x00);
		atp_writeb_io(dev, c, 1, 0x08);
		atp_writeb_io(dev, c, 2, 0x7f);
		atp_writeb_io(dev, c, 3, satn[0]);
		atp_writeb_io(dev, c, 4, satn[1]);
		atp_writeb_io(dev, c, 5, satn[2]);
		atp_writeb_io(dev, c, 6, satn[3]);
		atp_writeb_io(dev, c, 7, satn[4]);
		atp_writeb_io(dev, c, 8, satn[5]);
		atp_writeb_io(dev, c, 0x0f, 0);
		atp_writeb_io(dev, c, 0x11, dev->id[c][i].devsp);
		atp_writeb_io(dev, c, 0x12, 0);
		atp_writeb_io(dev, c, 0x13, satn[6]);
		atp_writeb_io(dev, c, 0x14, satn[7]);
		j = i;
		if ((j & 0x08) != 0) {
			j = (j & 0x07) | 0x40;
		}
		atp_writeb_io(dev, c, 0x15, j);
		atp_writeb_io(dev, c, 0x18, satn[8]);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00)
			cpu_relax();

		if (atp_readb_io(dev, c, 0x17) != 0x11 && atp_readb_io(dev, c, 0x17) != 0x8e)
			continue;

		while (atp_readb_io(dev, c, 0x17) != 0x8e)
			cpu_relax();

		dev->active_id[c] |= m;

		atp_writeb_io(dev, c, 0x10, 0x30);
		if (is885(dev) || is880(dev))
			atp_writeb_io(dev, c, 0x14, 0x00);
		else /* result of is870() merge - is this a bug? */
			atp_writeb_io(dev, c, 0x04, 0x00);

phase_cmd:
		atp_writeb_io(dev, c, 0x18, 0x08);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00)
			cpu_relax();

		j = atp_readb_io(dev, c, 0x17);
		if (j != 0x16) {
			atp_writeb_io(dev, c, 0x10, 0x41);
			goto phase_cmd;
		}
sel_ok:
		atp_writeb_io(dev, c, 3, inqd[0]);
		atp_writeb_io(dev, c, 4, inqd[1]);
		atp_writeb_io(dev, c, 5, inqd[2]);
		atp_writeb_io(dev, c, 6, inqd[3]);
		atp_writeb_io(dev, c, 7, inqd[4]);
		atp_writeb_io(dev, c, 8, inqd[5]);
		atp_writeb_io(dev, c, 0x0f, 0);
		atp_writeb_io(dev, c, 0x11, dev->id[c][i].devsp);
		atp_writeb_io(dev, c, 0x12, 0);
		atp_writeb_io(dev, c, 0x13, inqd[6]);
		atp_writeb_io(dev, c, 0x14, inqd[7]);
		atp_writeb_io(dev, c, 0x18, inqd[8]);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00)
			cpu_relax();

		if (atp_readb_io(dev, c, 0x17) != 0x11 && atp_readb_io(dev, c, 0x17) != 0x8e)
			continue;

		while (atp_readb_io(dev, c, 0x17) != 0x8e)
			cpu_relax();

		if (wide_chip)
			atp_writeb_io(dev, c, 0x1b, 0x00);

		atp_writeb_io(dev, c, 0x18, 0x08);
		j = 0;
rd_inq_data:
		k = atp_readb_io(dev, c, 0x1f);
		if ((k & 0x01) != 0) {
			mbuf[j++] = atp_readb_io(dev, c, 0x19);
			goto rd_inq_data;
		}
		if ((k & 0x80) == 0) {
			goto rd_inq_data;
		}
		j = atp_readb_io(dev, c, 0x17);
		if (j == 0x16) {
			goto inq_ok;
		}
		atp_writeb_io(dev, c, 0x10, 0x46);
		atp_writeb_io(dev, c, 0x12, 0);
		atp_writeb_io(dev, c, 0x13, 0);
		atp_writeb_io(dev, c, 0x14, 0);
		atp_writeb_io(dev, c, 0x18, 0x08);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00)
			cpu_relax();

		if (atp_readb_io(dev, c, 0x17) != 0x16)
			goto sel_ok;

inq_ok:
		mbuf[36] = 0;
		printk(KERN_INFO "         ID: %2d  %s\n", i, &mbuf[8]);
		dev->id[c][i].devtype = mbuf[0];
		rmb = mbuf[1];
		n = mbuf[7];
		if (!wide_chip)
			goto not_wide;
		if ((mbuf[7] & 0x60) == 0) {
			goto not_wide;
		}
		if (is885(dev) || is880(dev)) {
			if ((i < 8) && ((dev->global_map[c] & 0x20) == 0))
				goto not_wide;
		} else { /* result of is870() merge - is this a bug? */
			if ((dev->global_map[c] & 0x20) == 0)
				goto not_wide;
		}
		if (lvdmode == 0) {
			goto chg_wide;
		}
		if (dev->sp[c][i] != 0x04)	// force u2
		{
			goto chg_wide;
		}

		atp_writeb_io(dev, c, 0x1b, 0x01);
		atp_writeb_io(dev, c, 3, satn[0]);
		atp_writeb_io(dev, c, 4, satn[1]);
		atp_writeb_io(dev, c, 5, satn[2]);
		atp_writeb_io(dev, c, 6, satn[3]);
		atp_writeb_io(dev, c, 7, satn[4]);
		atp_writeb_io(dev, c, 8, satn[5]);
		atp_writeb_io(dev, c, 0x0f, 0);
		atp_writeb_io(dev, c, 0x11, dev->id[c][i].devsp);
		atp_writeb_io(dev, c, 0x12, 0);
		atp_writeb_io(dev, c, 0x13, satn[6]);
		atp_writeb_io(dev, c, 0x14, satn[7]);
		atp_writeb_io(dev, c, 0x18, satn[8]);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00)
			cpu_relax();

		if (atp_readb_io(dev, c, 0x17) != 0x11 && atp_readb_io(dev, c, 0x17) != 0x8e)
			continue;

		while (atp_readb_io(dev, c, 0x17) != 0x8e)
			cpu_relax();

try_u3:
		j = 0;
		atp_writeb_io(dev, c, 0x14, 0x09);
		atp_writeb_io(dev, c, 0x18, 0x20);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0) {
			if ((atp_readb_io(dev, c, 0x1f) & 0x01) != 0)
				atp_writeb_io(dev, c, 0x19, u3[j++]);
			cpu_relax();
		}

		while ((atp_readb_io(dev, c, 0x17) & 0x80) == 0x00)
			cpu_relax();

		j = atp_readb_io(dev, c, 0x17) & 0x0f;
		if (j == 0x0f) {
			goto u3p_in;
		}
		if (j == 0x0a) {
			goto u3p_cmd;
		}
		if (j == 0x0e) {
			goto try_u3;
		}
		continue;
u3p_out:
		atp_writeb_io(dev, c, 0x18, 0x20);
		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0) {
			if ((atp_readb_io(dev, c, 0x1f) & 0x01) != 0)
				atp_writeb_io(dev, c, 0x19, 0);
			cpu_relax();
		}
		j = atp_readb_io(dev, c, 0x17) & 0x0f;
		if (j == 0x0f) {
			goto u3p_in;
		}
		if (j == 0x0a) {
			goto u3p_cmd;
		}
		if (j == 0x0e) {
			goto u3p_out;
		}
		continue;
u3p_in:
		atp_writeb_io(dev, c, 0x14, 0x09);
		atp_writeb_io(dev, c, 0x18, 0x20);
		k = 0;
u3p_in1:
		j = atp_readb_io(dev, c, 0x1f);
		if ((j & 0x01) != 0) {
			mbuf[k++] = atp_readb_io(dev, c, 0x19);
			goto u3p_in1;
		}
		if ((j & 0x80) == 0x00) {
			goto u3p_in1;
		}
		j = atp_readb_io(dev, c, 0x17) & 0x0f;
		if (j == 0x0f) {
			goto u3p_in;
		}
		if (j == 0x0a) {
			goto u3p_cmd;
		}
		if (j == 0x0e) {
			goto u3p_out;
		}
		continue;
u3p_cmd:
		atp_writeb_io(dev, c, 0x10, 0x30);
		atp_writeb_io(dev, c, 0x14, 0x00);
		atp_writeb_io(dev, c, 0x18, 0x08);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00);

		j = atp_readb_io(dev, c, 0x17);
		if (j != 0x16) {
			if (j == 0x4e) {
				goto u3p_out;
			}
			continue;
		}
		if (mbuf[0] != 0x01) {
			goto chg_wide;
		}
		if (mbuf[1] != 0x06) {
			goto chg_wide;
		}
		if (mbuf[2] != 0x04) {
			goto chg_wide;
		}
		if (mbuf[3] == 0x09) {
			m = 1;
			m = m << i;
			dev->wide_id[c] |= m;
			dev->id[c][i].devsp = 0xce;
#ifdef ED_DBGP		   
			printk("dev->id[%2d][%2d].devsp = %2x\n",c,i,dev->id[c][i].devsp);
#endif
			continue;
		}
chg_wide:
		atp_writeb_io(dev, c, 0x1b, 0x01);
		atp_writeb_io(dev, c, 3, satn[0]);
		atp_writeb_io(dev, c, 4, satn[1]);
		atp_writeb_io(dev, c, 5, satn[2]);
		atp_writeb_io(dev, c, 6, satn[3]);
		atp_writeb_io(dev, c, 7, satn[4]);
		atp_writeb_io(dev, c, 8, satn[5]);
		atp_writeb_io(dev, c, 0x0f, 0);
		atp_writeb_io(dev, c, 0x11, dev->id[c][i].devsp);
		atp_writeb_io(dev, c, 0x12, 0);
		atp_writeb_io(dev, c, 0x13, satn[6]);
		atp_writeb_io(dev, c, 0x14, satn[7]);
		atp_writeb_io(dev, c, 0x18, satn[8]);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00)
			cpu_relax();

		if (atp_readb_io(dev, c, 0x17) != 0x11 && atp_readb_io(dev, c, 0x17) != 0x8e)
			continue;

		while (atp_readb_io(dev, c, 0x17) != 0x8e)
			cpu_relax();

try_wide:
		j = 0;
		atp_writeb_io(dev, c, 0x14, 0x05);
		atp_writeb_io(dev, c, 0x18, 0x20);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0) {
			if ((atp_readb_io(dev, c, 0x1f) & 0x01) != 0)
				atp_writeb_io(dev, c, 0x19, wide[j++]);
			cpu_relax();
		}

		while ((atp_readb_io(dev, c, 0x17) & 0x80) == 0x00)
			cpu_relax();

		j = atp_readb_io(dev, c, 0x17) & 0x0f;
		if (j == 0x0f) {
			goto widep_in;
		}
		if (j == 0x0a) {
			goto widep_cmd;
		}
		if (j == 0x0e) {
			goto try_wide;
		}
		continue;
widep_out:
		atp_writeb_io(dev, c, 0x18, 0x20);
		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0) {
			if ((atp_readb_io(dev, c, 0x1f) & 0x01) != 0)
				atp_writeb_io(dev, c, 0x19, 0);
			cpu_relax();
		}
		j = atp_readb_io(dev, c, 0x17) & 0x0f;
		if (j == 0x0f) {
			goto widep_in;
		}
		if (j == 0x0a) {
			goto widep_cmd;
		}
		if (j == 0x0e) {
			goto widep_out;
		}
		continue;
widep_in:
		atp_writeb_io(dev, c, 0x14, 0xff);
		atp_writeb_io(dev, c, 0x18, 0x20);
		k = 0;
widep_in1:
		j = atp_readb_io(dev, c, 0x1f);
		if ((j & 0x01) != 0) {
			mbuf[k++] = atp_readb_io(dev, c, 0x19);
			goto widep_in1;
		}
		if ((j & 0x80) == 0x00) {
			goto widep_in1;
		}
		j = atp_readb_io(dev, c, 0x17) & 0x0f;
		if (j == 0x0f) {
			goto widep_in;
		}
		if (j == 0x0a) {
			goto widep_cmd;
		}
		if (j == 0x0e) {
			goto widep_out;
		}
		continue;
widep_cmd:
		atp_writeb_io(dev, c, 0x10, 0x30);
		atp_writeb_io(dev, c, 0x14, 0x00);
		atp_writeb_io(dev, c, 0x18, 0x08);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00)
			cpu_relax();

		j = atp_readb_io(dev, c, 0x17);
		if (j != 0x16) {
			if (j == 0x4e) {
				goto widep_out;
			}
			continue;
		}
		if (mbuf[0] != 0x01) {
			goto not_wide;
		}
		if (mbuf[1] != 0x02) {
			goto not_wide;
		}
		if (mbuf[2] != 0x03) {
			goto not_wide;
		}
		if (mbuf[3] != 0x01) {
			goto not_wide;
		}
		m = 1;
		m = m << i;
		dev->wide_id[c] |= m;
not_wide:
		if ((dev->id[c][i].devtype == 0x00) || (dev->id[c][i].devtype == 0x07) || ((dev->id[c][i].devtype == 0x05) && ((n & 0x10) != 0))) {
			m = 1;
			m = m << i;
			if ((dev->async[c] & m) != 0) {
				goto set_sync;
			}
		}
		continue;
set_sync:
		if ((!is885(dev) && !is880(dev)) || (dev->sp[c][i] == 0x02)) {
			synu[4] = 0x0c;
			synuw[4] = 0x0c;
		} else {
			if (dev->sp[c][i] >= 0x03) {
				synu[4] = 0x0a;
				synuw[4] = 0x0a;
			}
		}
		j = 0;
		if ((m & dev->wide_id[c]) != 0) {
			j |= 0x01;
		}
		atp_writeb_io(dev, c, 0x1b, j);
		atp_writeb_io(dev, c, 3, satn[0]);
		atp_writeb_io(dev, c, 4, satn[1]);
		atp_writeb_io(dev, c, 5, satn[2]);
		atp_writeb_io(dev, c, 6, satn[3]);
		atp_writeb_io(dev, c, 7, satn[4]);
		atp_writeb_io(dev, c, 8, satn[5]);
		atp_writeb_io(dev, c, 0x0f, 0);
		atp_writeb_io(dev, c, 0x11, dev->id[c][i].devsp);
		atp_writeb_io(dev, c, 0x12, 0);
		atp_writeb_io(dev, c, 0x13, satn[6]);
		atp_writeb_io(dev, c, 0x14, satn[7]);
		atp_writeb_io(dev, c, 0x18, satn[8]);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00)
			cpu_relax();

		if (atp_readb_io(dev, c, 0x17) != 0x11 && atp_readb_io(dev, c, 0x17) != 0x8e)
			continue;

		while (atp_readb_io(dev, c, 0x17) != 0x8e)
			cpu_relax();

try_sync:
		j = 0;
		atp_writeb_io(dev, c, 0x14, 0x06);
		atp_writeb_io(dev, c, 0x18, 0x20);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0) {
			if ((atp_readb_io(dev, c, 0x1f) & 0x01) != 0) {
				if ((m & dev->wide_id[c]) != 0) {
					if (is885(dev) || is880(dev)) {
						if ((m & dev->ultra_map[c]) != 0) {
							atp_writeb_io(dev, c, 0x19, synuw[j++]);
						} else {
							atp_writeb_io(dev, c, 0x19, synw[j++]);
						}
					} else
						atp_writeb_io(dev, c, 0x19, synw_870[j++]);
				} else {
					if ((m & dev->ultra_map[c]) != 0) {
						atp_writeb_io(dev, c, 0x19, synu[j++]);
					} else {
						atp_writeb_io(dev, c, 0x19, synn[j++]);
					}
				}
			}
		}

		while ((atp_readb_io(dev, c, 0x17) & 0x80) == 0x00)
			cpu_relax();

		j = atp_readb_io(dev, c, 0x17) & 0x0f;
		if (j == 0x0f) {
			goto phase_ins;
		}
		if (j == 0x0a) {
			goto phase_cmds;
		}
		if (j == 0x0e) {
			goto try_sync;
		}
		continue;
phase_outs:
		atp_writeb_io(dev, c, 0x18, 0x20);
		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00) {
			if ((atp_readb_io(dev, c, 0x1f) & 0x01) != 0x00)
				atp_writeb_io(dev, c, 0x19, 0x00);
			cpu_relax();
		}
		j = atp_readb_io(dev, c, 0x17);
		if (j == 0x85) {
			goto tar_dcons;
		}
		j &= 0x0f;
		if (j == 0x0f) {
			goto phase_ins;
		}
		if (j == 0x0a) {
			goto phase_cmds;
		}
		if (j == 0x0e) {
			goto phase_outs;
		}
		continue;
phase_ins:
		if (is885(dev) || is880(dev))
			atp_writeb_io(dev, c, 0x14, 0x06);
		else
			atp_writeb_io(dev, c, 0x14, 0xff);
		atp_writeb_io(dev, c, 0x18, 0x20);
		k = 0;
phase_ins1:
		j = atp_readb_io(dev, c, 0x1f);
		if ((j & 0x01) != 0x00) {
			mbuf[k++] = atp_readb_io(dev, c, 0x19);
			goto phase_ins1;
		}
		if ((j & 0x80) == 0x00) {
			goto phase_ins1;
		}

		while ((atp_readb_io(dev, c, 0x17) & 0x80) == 0x00);

		j = atp_readb_io(dev, c, 0x17);
		if (j == 0x85) {
			goto tar_dcons;
		}
		j &= 0x0f;
		if (j == 0x0f) {
			goto phase_ins;
		}
		if (j == 0x0a) {
			goto phase_cmds;
		}
		if (j == 0x0e) {
			goto phase_outs;
		}
		continue;
phase_cmds:
		atp_writeb_io(dev, c, 0x10, 0x30);
tar_dcons:
		atp_writeb_io(dev, c, 0x14, 0x00);
		atp_writeb_io(dev, c, 0x18, 0x08);

		while ((atp_readb_io(dev, c, 0x1f) & 0x80) == 0x00)
			cpu_relax();

		j = atp_readb_io(dev, c, 0x17);
		if (j != 0x16) {
			continue;
		}
		if (mbuf[0] != 0x01) {
			continue;
		}
		if (mbuf[1] != 0x03) {
			continue;
		}
		if (mbuf[4] == 0x00) {
			continue;
		}
		if (mbuf[3] > 0x64) {
			continue;
		}
		if (is885(dev) || is880(dev)) {
			if (mbuf[4] > 0x0e) {
				mbuf[4] = 0x0e;
			}
		} else {
			if (mbuf[4] > 0x0c) {
				mbuf[4] = 0x0c;
			}
		}
		dev->id[c][i].devsp = mbuf[4];
		if (is885(dev) || is880(dev))
			if (mbuf[3] < 0x0c) {
				j = 0xb0;
				goto set_syn_ok;
			}
		if ((mbuf[3] < 0x0d) && (rmb == 0)) {
			j = 0xa0;
			goto set_syn_ok;
		}
		if (mbuf[3] < 0x1a) {
			j = 0x20;
			goto set_syn_ok;
		}
		if (mbuf[3] < 0x33) {
			j = 0x40;
			goto set_syn_ok;
		}
		if (mbuf[3] < 0x4c) {
			j = 0x50;
			goto set_syn_ok;
		}
		j = 0x60;
set_syn_ok:
		dev->id[c][i].devsp = (dev->id[c][i].devsp & 0x0f) | j;
#ifdef ED_DBGP
		printk("dev->id[%2d][%2d].devsp = %2x\n",c,i,dev->id[c][i].devsp);
#endif
	}
}
