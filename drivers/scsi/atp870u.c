/* 
 *  Copyright (C) 1997	Wu Ching Chen
 *  2.1.x update (C) 1998  Krzysztof G. Baranowski
 *  2.5.x update (C) 2002  Red Hat <alan@redhat.com>
 *  2.6.x update (C) 2004  Red Hat <alan@redhat.com>
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
#include <asm/system.h>
#include <asm/io.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "atp870u.h"

static struct scsi_host_template atp870u_template;
static void send_s870(struct atp_unit *dev,unsigned char c);
static void is885(struct atp_unit *dev, unsigned int wkport,unsigned char c);
static void tscam_885(void);

static irqreturn_t atp870u_intr_handle(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	unsigned short int tmpcip, id;
	unsigned char i, j, c, target_id, lun,cmdp;
	unsigned char *prd;
	struct scsi_cmnd *workreq;
	unsigned int workport, tmport, tmport1;
	unsigned long adrcnt, k;
#ifdef ED_DBGP
	unsigned long l;
#endif
	int errstus;
	struct Scsi_Host *host = dev_id;
	struct atp_unit *dev = (struct atp_unit *)&host->hostdata;

	for (c = 0; c < 2; c++) {
		tmport = dev->ioport[c] + 0x1f;
		j = inb(tmport);
		if ((j & 0x80) != 0)
		{			
	   		goto ch_sel;
		}
		dev->in_int[c] = 0;
	}
	return IRQ_NONE;
ch_sel:
#ifdef ED_DBGP	
	printk("atp870u_intr_handle enter\n");
#endif	
	dev->in_int[c] = 1;
	cmdp = inb(dev->ioport[c] + 0x10);
	workport = dev->ioport[c];
	if (dev->working[c] != 0) {
		if (dev->dev_id == ATP885_DEVID) {
			tmport1 = workport + 0x16;
			if ((inb(tmport1) & 0x80) == 0)
				outb((inb(tmport1) | 0x80), tmport1);
		}		
		tmpcip = dev->pciport[c];
		if ((inb(tmpcip) & 0x08) != 0)
		{
			tmpcip += 0x2;
			for (k=0; k < 1000; k++) {
				if ((inb(tmpcip) & 0x08) == 0) {
					goto stop_dma;
				}
				if ((inb(tmpcip) & 0x01) == 0) {
					goto stop_dma;
				}
			}
		}
stop_dma:
		tmpcip = dev->pciport[c];
		outb(0x00, tmpcip);
		tmport -= 0x08;
		
		i = inb(tmport);
		
		if (dev->dev_id == ATP885_DEVID) {
			tmpcip += 2;
			outb(0x06, tmpcip);
			tmpcip -= 2;
		}

		tmport -= 0x02;
		target_id = inb(tmport);
		tmport += 0x02;

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
		if (dev->dev_id == ATP885_DEVID) 
			dev->r1f[c][target_id] |= j;
#ifdef ED_DBGP
		printk("atp870u_intr_handle status = %x\n",i);
#endif	
		if (i == 0x85) {
			if ((dev->last_cmd[c] & 0xf0) != 0x40) {
			   dev->last_cmd[c] = 0xff;
			}
			if (dev->dev_id == ATP885_DEVID) {
				tmport -= 0x05;
				adrcnt = 0;
				((unsigned char *) &adrcnt)[2] = inb(tmport++);
				((unsigned char *) &adrcnt)[1] = inb(tmport++);
				((unsigned char *) &adrcnt)[0] = inb(tmport);
				if (dev->id[c][target_id].last_len != adrcnt)
				{
			   		k = dev->id[c][target_id].last_len;
			   		k -= adrcnt;
			   		dev->id[c][target_id].tran_len = k;			   
			   	dev->id[c][target_id].last_len = adrcnt;			   
				}
#ifdef ED_DBGP
				printk("tmport = %x dev->id[c][target_id].last_len = %d dev->id[c][target_id].tran_len = %d\n",tmport,dev->id[c][target_id].last_len,dev->id[c][target_id].tran_len);
#endif		
			}

			/*
			 *      Flip wide
			 */			
			if (dev->wide_id[c] != 0) {
				tmport = workport + 0x1b;
				outb(0x01, tmport);
				while ((inb(tmport) & 0x01) != 0x01) {
					outb(0x01, tmport);
				}
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
			goto handled;
		}

		if (i == 0x40) {
		     dev->last_cmd[c] |= 0x40;
		     dev->in_int[c] = 0;
		     goto handled;
		}

		if (i == 0x21) {
			if ((dev->last_cmd[c] & 0xf0) != 0x40) {
			   dev->last_cmd[c] = 0xff;
			}
			tmport -= 0x05;
			adrcnt = 0;
			((unsigned char *) &adrcnt)[2] = inb(tmport++);
			((unsigned char *) &adrcnt)[1] = inb(tmport++);
			((unsigned char *) &adrcnt)[0] = inb(tmport);
			k = dev->id[c][target_id].last_len;
			k -= adrcnt;
			dev->id[c][target_id].tran_len = k;
			dev->id[c][target_id].last_len = adrcnt;
			tmport -= 0x04;
			outb(0x41, tmport);
			tmport += 0x08;
			outb(0x08, tmport);
			dev->in_int[c] = 0;
			goto handled;
		}

		if (dev->dev_id == ATP885_DEVID) {
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
			tmport -= 0x07;
			if (cmdp == 0x44 || i==0x80) {
				tmport += 0x0d;
				lun = inb(tmport) & 0x07;
			} else {
				if ((dev->last_cmd[c] & 0xf0) != 0x40) {
				   dev->last_cmd[c] = 0xff;
				}
				if (cmdp == 0x41) {
#ifdef ED_DBGP
					printk("cmdp = 0x41\n");
#endif						
					tmport += 0x02;
					adrcnt = 0;
					((unsigned char *) &adrcnt)[2] = inb(tmport++);
					((unsigned char *) &adrcnt)[1] = inb(tmport++);
					((unsigned char *) &adrcnt)[0] = inb(tmport);
					k = dev->id[c][target_id].last_len;
					k -= adrcnt;
					dev->id[c][target_id].tran_len = k;
					dev->id[c][target_id].last_len = adrcnt;
					tmport += 0x04;
					outb(0x08, tmport);
					dev->in_int[c] = 0;
					goto handled;
				} else {
#ifdef ED_DBGP
					printk("cmdp != 0x41\n");
#endif						
					outb(0x46, tmport);
					dev->id[c][target_id].dirct = 0x00;
					tmport += 0x02;
					outb(0x00, tmport++);
					outb(0x00, tmport++);
					outb(0x00, tmport++);
					tmport += 0x03;
					outb(0x08, tmport);
					dev->in_int[c] = 0;
					goto handled;
				}
			}
			if (dev->last_cmd[c] != 0xff) {
			   dev->last_cmd[c] |= 0x40;
			}
			if (dev->dev_id == ATP885_DEVID) {
				j = inb(dev->baseport + 0x29) & 0xfe;
				outb(j, dev->baseport + 0x29);
				tmport = workport + 0x16;
			} else {
				tmport = workport + 0x10;
				outb(0x45, tmport);
				tmport += 0x06;				
			}
			
			target_id = inb(tmport);
			/*
			 *	Remap wide identifiers
			 */
			if ((target_id & 0x10) != 0) {
				target_id = (target_id & 0x07) | 0x08;
			} else {
				target_id &= 0x07;
			}
			if (dev->dev_id == ATP885_DEVID) {
				tmport = workport + 0x10;
				outb(0x45, tmport);
			}
			workreq = dev->id[c][target_id].curr_req;
#ifdef ED_DBGP			
			printk(KERN_DEBUG "Channel = %d ID = %d LUN = %d CDB",c,workreq->device->id,workreq->device->lun);
			for(l=0;l<workreq->cmd_len;l++)
			{
				printk(KERN_DEBUG " %x",workreq->cmnd[l]);
			}
#endif	
			
			tmport = workport + 0x0f;
			outb(lun, tmport);
			tmport += 0x02;
			outb(dev->id[c][target_id].devsp, tmport++);
			adrcnt = dev->id[c][target_id].tran_len;
			k = dev->id[c][target_id].last_len;

			outb(((unsigned char *) &k)[2], tmport++);
			outb(((unsigned char *) &k)[1], tmport++);
			outb(((unsigned char *) &k)[0], tmport++);
#ifdef ED_DBGP			
			printk("k %x, k[0] 0x%x k[1] 0x%x k[2] 0x%x\n", k, inb(tmport-1), inb(tmport-2), inb(tmport-3));
#endif			
			/* Remap wide */
			j = target_id;
			if (target_id > 7) {
				j = (j & 0x07) | 0x40;
			}
			/* Add direction */
			j |= dev->id[c][target_id].dirct;
			outb(j, tmport++);
			outb(0x80,tmport);
			
			/* enable 32 bit fifo transfer */	
			if (dev->dev_id == ATP885_DEVID) {
				tmpcip = dev->pciport[c] + 1;
				i=inb(tmpcip) & 0xf3;
				//j=workreq->cmnd[0];	    		    	
				if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) || (workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a)) {
				   i |= 0x0c;
				}
				outb(i,tmpcip);		    		    		
			} else if ((dev->dev_id == ATP880_DEVID1) ||
	    		    	   (dev->dev_id == ATP880_DEVID2) ) {
				tmport = workport - 0x05;
				if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) || (workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a)) {
					outb((unsigned char) ((inb(tmport) & 0x3f) | 0xc0), tmport);
				} else {
					outb((unsigned char) (inb(tmport) & 0x3f), tmport);
				}
			} else {				
				tmport = workport + 0x3a;
				if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) || (workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a)) {
					outb((unsigned char) ((inb(tmport) & 0xf3) | 0x08), tmport);
				} else {
					outb((unsigned char) (inb(tmport) & 0xf3), tmport);
				}														
			}	
			tmport = workport + 0x1b;
			j = 0;
			id = 1;
			id = id << target_id;
			/*
			 *	Is this a wide device
			 */
			if ((id & dev->wide_id[c]) != 0) {
				j |= 0x01;
			}
			outb(j, tmport);
			while ((inb(tmport) & 0x01) != j) {
				outb(j,tmport);
			}
			if (dev->id[c][target_id].last_len == 0) {
				tmport = workport + 0x18;
				outb(0x08, tmport);
				dev->in_int[c] = 0;
#ifdef ED_DBGP
				printk("dev->id[c][target_id].last_len = 0\n");
#endif					
				goto handled;
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
			tmpcip = dev->pciport[c] + 0x04;
			outl(dev->id[c][target_id].prdaddr, tmpcip);
#ifdef ED_DBGP
			printk("dev->id[%d][%d].prdaddr 0x%8x\n", c, target_id, dev->id[c][target_id].prdaddr);
#endif
			if (dev->dev_id == ATP885_DEVID) {
				tmpcip -= 0x04;
			} else {
				tmpcip -= 0x02;
				outb(0x06, tmpcip);
				outb(0x00, tmpcip);
				tmpcip -= 0x02;
			}
			tmport = workport + 0x18;
			/*
			 *	Check transfer direction
			 */
			if (dev->id[c][target_id].dirct != 0) {
				outb(0x08, tmport);
				outb(0x01, tmpcip);
				dev->in_int[c] = 0;
#ifdef ED_DBGP
				printk("status 0x80 return dirct != 0\n");
#endif				
				goto handled;
			}
			outb(0x08, tmport);
			outb(0x09, tmpcip);
			dev->in_int[c] = 0;
#ifdef ED_DBGP
			printk("status 0x80 return dirct = 0\n");
#endif			
			goto handled;
		}

		/*
		 *	Current scsi request on this target
		 */

		workreq = dev->id[c][target_id].curr_req;

		if (i == 0x42) {
			if ((dev->last_cmd[c] & 0xf0) != 0x40)
			{
			   dev->last_cmd[c] = 0xff;
			}
			errstus = 0x02;
			workreq->result = errstus;
			goto go_42;
		}
		if (i == 0x16) {
			if ((dev->last_cmd[c] & 0xf0) != 0x40) {
			   dev->last_cmd[c] = 0xff;
			}
			errstus = 0;
			tmport -= 0x08;
			errstus = inb(tmport);
			if (((dev->r1f[c][target_id] & 0x10) != 0)&&(dev->dev_id==ATP885_DEVID)) {
			   printk(KERN_WARNING "AEC67162 CRC ERROR !\n");
			   errstus = 0x02;
			}
			workreq->result = errstus;
go_42:
			if (dev->dev_id == ATP885_DEVID) {		
				j = inb(dev->baseport + 0x29) | 0x01;
				outb(j, dev->baseport + 0x29);
			}
			/*
			 *	Complete the command
			 */
			if (workreq->use_sg) {
				pci_unmap_sg(dev->pdev,
					(struct scatterlist *)workreq->buffer,
					workreq->use_sg,
					workreq->sc_data_direction);
			} else if (workreq->request_bufflen &&
					workreq->sc_data_direction != DMA_NONE) {
				pci_unmap_single(dev->pdev,
					workreq->SCp.dma_handle,
					workreq->request_bufflen,
					workreq->sc_data_direction);
			}			
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
				tmport = workport + 0x1b;
				outb(0x01, tmport);
				while ((inb(tmport) & 0x01) != 0x01) {
					outb(0x01, tmport);
				}       
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
			goto handled;
		}
		if ((dev->last_cmd[c] & 0xf0) != 0x40) {
		   dev->last_cmd[c] = 0xff;
		}
		if (i == 0x4f) {
			i = 0x89;
		}
		i &= 0x0f;
		if (i == 0x09) {
			tmpcip += 4;
			outl(dev->id[c][target_id].prdaddr, tmpcip);
			tmpcip = tmpcip - 2;
			outb(0x06, tmpcip);
			outb(0x00, tmpcip);
			tmpcip = tmpcip - 2;
			tmport = workport + 0x10;
			outb(0x41, tmport);
			if (dev->dev_id == ATP885_DEVID) {
				tmport += 2;
				k = dev->id[c][target_id].last_len;
				outb((unsigned char) (((unsigned char *) (&k))[2]), tmport++);
				outb((unsigned char) (((unsigned char *) (&k))[1]), tmport++);
				outb((unsigned char) (((unsigned char *) (&k))[0]), tmport);
				dev->id[c][target_id].dirct = 0x00;
				tmport += 0x04;
			} else {
				dev->id[c][target_id].dirct = 0x00;
				tmport += 0x08;				
			}
			outb(0x08, tmport);
			outb(0x09, tmpcip);
			dev->in_int[c] = 0;
			goto handled;
		}
		if (i == 0x08) {
			tmpcip += 4;
			outl(dev->id[c][target_id].prdaddr, tmpcip);
			tmpcip = tmpcip - 2;
			outb(0x06, tmpcip);
			outb(0x00, tmpcip);
			tmpcip = tmpcip - 2;
			tmport = workport + 0x10;
			outb(0x41, tmport);
			if (dev->dev_id == ATP885_DEVID) {		
				tmport += 2;
				k = dev->id[c][target_id].last_len;
				outb((unsigned char) (((unsigned char *) (&k))[2]), tmport++);
				outb((unsigned char) (((unsigned char *) (&k))[1]), tmport++);
				outb((unsigned char) (((unsigned char *) (&k))[0]), tmport++);
			} else {
				tmport += 5;
			}
			outb((unsigned char) (inb(tmport) | 0x20), tmport);
			dev->id[c][target_id].dirct = 0x20;
			tmport += 0x03;
			outb(0x08, tmport);
			outb(0x01, tmpcip);
			dev->in_int[c] = 0;
			goto handled;
		}
		tmport -= 0x07;
		if (i == 0x0a) {
			outb(0x30, tmport);
		} else {
			outb(0x46, tmport);
		}
		dev->id[c][target_id].dirct = 0x00;
		tmport += 0x02;
		outb(0x00, tmport++);
		outb(0x00, tmport++);
		outb(0x00, tmport++);
		tmport += 0x03;
		outb(0x08, tmport);
		dev->in_int[c] = 0;
		goto handled;
	} else {
//		tmport = workport + 0x17;
//		inb(tmport);
//		dev->working[c] = 0;
		dev->in_int[c] = 0;
		goto handled;
	}
	
handled:
#ifdef ED_DBGP
	printk("atp870u_intr_handle exit\n");
#endif			
	return IRQ_HANDLED;
}
/**
 *	atp870u_queuecommand	-	Queue SCSI command
 *	@req_p: request block
 *	@done: completion function
 *
 *	Queue a command to the ATP queue. Called with the host lock held.
 */
static int atp870u_queuecommand(struct scsi_cmnd * req_p, 
			 void (*done) (struct scsi_cmnd *))
{
	unsigned char c;
	unsigned int tmport,m;	
	struct atp_unit *dev;
	struct Scsi_Host *host;

	c = req_p->device->channel;	
	req_p->sense_buffer[0]=0;
	req_p->resid = 0;
	if (req_p->device->channel > 1) {
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
	m = m << req_p->device->id;

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
	tmport = dev->ioport[c] + 0x1c;
#ifdef ED_DBGP	
	printk("dev->ioport[c] = %x inb(tmport) = %x dev->in_int[%d] = %d dev->in_snd[%d] = %d\n",dev->ioport[c],inb(tmport),c,dev->in_int[c],c,dev->in_snd[c]);
#endif
	if ((inb(tmport) == 0) && (dev->in_int[c] == 0) && (dev->in_snd[c] == 0)) {
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
	unsigned int tmport;
	struct scsi_cmnd *workreq;
	unsigned int i;//,k;
	unsigned char  j, target_id;
	unsigned char *prd;
	unsigned short int tmpcip, w;
	unsigned long l, bttl = 0;
	unsigned int workport;
	struct scatterlist *sgpnt;
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
		if (workreq != NULL) {	/* check NULL pointer */
		   goto cmd_subp;
		}
		dev->last_cmd[c] = 0xff;	
		if (dev->quhd[c] == dev->quend[c]) {
		   	dev->in_snd[c] = 0;
		   	return ;
		}
	}
	if ((dev->last_cmd[c] != 0xff) && (dev->working[c] != 0)) {
	     	dev->in_snd[c] = 0;
	     	return ;
	}
	dev->working[c]++;
	j = dev->quhd[c];
	dev->quhd[c]++;
	if (dev->quhd[c] >= qcnt) {
		dev->quhd[c] = 0;
	}
	workreq = dev->quereq[c][dev->quhd[c]];
	if (dev->id[c][workreq->device->id].curr_req == 0) {	
		dev->id[c][workreq->device->id].curr_req = workreq;
		dev->last_cmd[c] = workreq->device->id;
		goto cmd_subp;
	}	
	dev->quhd[c] = j;
	dev->working[c]--;
	dev->in_snd[c] = 0;
	return;
cmd_subp:
	workport = dev->ioport[c];
	tmport = workport + 0x1f;
	if ((inb(tmport) & 0xb0) != 0) {
		goto abortsnd;
	}
	tmport = workport + 0x1c;
	if (inb(tmport) == 0) {
		goto oktosend;
	}
abortsnd:
#ifdef ED_DBGP
	printk("Abort to Send\n");
#endif
	dev->last_cmd[c] |= 0x40;
	dev->in_snd[c] = 0;
	return;
oktosend:
#ifdef ED_DBGP
	printk("OK to Send\n");
	printk("CDB");
	for(i=0;i<workreq->cmd_len;i++) {
		printk(" %x",workreq->cmnd[i]);
	}
	printk("\nChannel = %d ID = %d LUN = %d\n",c,workreq->device->id,workreq->device->lun);
#endif	
	if (dev->dev_id == ATP885_DEVID) {
		j = inb(dev->baseport + 0x29) & 0xfe;
		outb(j, dev->baseport + 0x29);
		dev->r1f[c][workreq->device->id] = 0;
	}
	
	if (workreq->cmnd[0] == READ_CAPACITY) {
		if (workreq->request_bufflen > 8) {
			workreq->request_bufflen = 0x08;
		}
	}
	if (workreq->cmnd[0] == 0x00) {
		workreq->request_bufflen = 0;
	}

	tmport = workport + 0x1b;
	j = 0;
	target_id = workreq->device->id;

	/*
	 *	Wide ?
	 */
	w = 1;
	w = w << target_id;
	if ((w & dev->wide_id[c]) != 0) {
		j |= 0x01;
	}
	outb(j, tmport);
	while ((inb(tmport) & 0x01) != j) {
		outb(j,tmport);
#ifdef ED_DBGP
		printk("send_s870 while loop 1\n");
#endif
	}
	/*
	 *	Write the command
	 */

	tmport = workport;
	outb(workreq->cmd_len, tmport++);
	outb(0x2c, tmport++);
	if (dev->dev_id == ATP885_DEVID) {
		outb(0x7f, tmport++);
	} else {
		outb(0xcf, tmport++); 	
	}	
	for (i = 0; i < workreq->cmd_len; i++) {
		outb(workreq->cmnd[i], tmport++);
	}
	tmport = workport + 0x0f;
	outb(workreq->device->lun, tmport);
	tmport += 0x02;
	/*
	 *	Write the target
	 */
	outb(dev->id[c][target_id].devsp, tmport++);	 
#ifdef ED_DBGP	
	printk("dev->id[%d][%d].devsp = %2x\n",c,target_id,dev->id[c][target_id].devsp);
#endif
	/*
	 *	Figure out the transfer size
	 */
	if (workreq->use_sg) {
#ifdef ED_DBGP
		printk("Using SGL\n");
#endif		
		l = 0;
		
		sgpnt = (struct scatterlist *) workreq->request_buffer;
		sg_count = pci_map_sg(dev->pdev, sgpnt, workreq->use_sg,
	   			workreq->sc_data_direction);		
		
		for (i = 0; i < workreq->use_sg; i++) {
			if (sgpnt[i].length == 0 || workreq->use_sg > ATP870U_SCATTER) {
				panic("Foooooooood fight!");
			}
			l += sgpnt[i].length;
		}
#ifdef ED_DBGP		
		printk( "send_s870: workreq->use_sg %d, sg_count %d l %8ld\n", workreq->use_sg, sg_count, l);
#endif
	} else if(workreq->request_bufflen && workreq->sc_data_direction != PCI_DMA_NONE) {
#ifdef ED_DBGP
		printk("Not using SGL\n");
#endif					
		workreq->SCp.dma_handle = pci_map_single(dev->pdev, workreq->request_buffer,
				workreq->request_bufflen,
				workreq->sc_data_direction);		
		l = workreq->request_bufflen;
#ifdef ED_DBGP		
		printk( "send_s870: workreq->use_sg %d, l %8ld\n", workreq->use_sg, l);
#endif
	} else l = 0;
	/*
	 *	Write transfer size
	 */
	outb((unsigned char) (((unsigned char *) (&l))[2]), tmport++);
	outb((unsigned char) (((unsigned char *) (&l))[1]), tmport++);
	outb((unsigned char) (((unsigned char *) (&l))[0]), tmport++);
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
	if (workreq->sc_data_direction == DMA_TO_DEVICE) {
		outb((unsigned char) (j | 0x20), tmport++);
	} else {
		outb(j, tmport++);
	}
	outb((unsigned char) (inb(tmport) | 0x80), tmport);
	outb(0x80, tmport);
	tmport = workport + 0x1c;
	dev->id[c][target_id].dirct = 0;
	if (l == 0) {
		if (inb(tmport) == 0) {
			tmport = workport + 0x18;
#ifdef ED_DBGP
			printk("change SCSI_CMD_REG 0x08\n");	
#endif				
			outb(0x08, tmport);
		} else {
			dev->last_cmd[c] |= 0x40;
		}
		dev->in_snd[c] = 0;
		return;
	}
	tmpcip = dev->pciport[c];
	prd = dev->id[c][target_id].prd_table;
	dev->id[c][target_id].prd_pos = prd;

	/*
	 *	Now write the request list. Either as scatter/gather or as
	 *	a linear chain.
	 */

	if (workreq->use_sg) {
		sgpnt = (struct scatterlist *) workreq->request_buffer;
		i = 0;
		for (j = 0; j < workreq->use_sg; j++) {
			bttl = sg_dma_address(&sgpnt[j]);
			l=sg_dma_len(&sgpnt[j]);
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
	} else {
		/*
		 *	For a linear request write a chain of blocks
		 */        
		bttl = workreq->SCp.dma_handle;
		l = workreq->request_bufflen;
		i = 0;
#ifdef ED_DBGP		
		printk("3. bttl %x, l %x\n",bttl, l);
#endif			
		while (l > 0x10000) {
				(((u16 *) (prd))[i + 3]) = 0x0000;
				(((u16 *) (prd))[i + 2]) = 0x0000;
				(((u32 *) (prd))[i >> 1]) = cpu_to_le32(bttl);
				l -= 0x10000;
				bttl += 0x10000;
				i += 0x04;
			}
			(((u16 *) (prd))[i + 3]) = cpu_to_le16(0x8000);
			(((u16 *) (prd))[i + 2]) = cpu_to_le16(l);
			(((u32 *) (prd))[i >> 1]) = cpu_to_le32(bttl);		
#ifdef ED_DBGP		
		printk("prd %4x %4x %4x %4x\n",(((unsigned short int *)prd)[0]),(((unsigned short int *)prd)[1]),(((unsigned short int *)prd)[2]),(((unsigned short int *)prd)[3]));
		printk("4. bttl %x, l %x\n",bttl, l);
#endif			
		
	}
	tmpcip += 4;
#ifdef ED_DBGP		
	printk("send_s870: prdaddr_2 0x%8x tmpcip %x target_id %d\n", dev->id[c][target_id].prdaddr,tmpcip,target_id);
#endif	
	dev->id[c][target_id].prdaddr = dev->id[c][target_id].prd_bus;
	outl(dev->id[c][target_id].prdaddr, tmpcip);
	tmpcip = tmpcip - 2;
	outb(0x06, tmpcip);
	outb(0x00, tmpcip);
	if (dev->dev_id == ATP885_DEVID) {
		tmpcip--;
		j=inb(tmpcip) & 0xf3;
		if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) ||
	    	(workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a)) {
	   		j |= 0x0c;
		}
		outb(j,tmpcip);
		tmpcip--;	    	
	} else if ((dev->dev_id == ATP880_DEVID1) ||
	    	   (dev->dev_id == ATP880_DEVID2)) {
		tmpcip =tmpcip -2;	
		tmport = workport - 0x05;
		if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) || (workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a)) {
			outb((unsigned char) ((inb(tmport) & 0x3f) | 0xc0), tmport);
		} else {
			outb((unsigned char) (inb(tmport) & 0x3f), tmport);
		}		
	} else {		
		tmpcip =tmpcip -2;
		tmport = workport + 0x3a;
		if ((workreq->cmnd[0] == 0x08) || (workreq->cmnd[0] == 0x28) || (workreq->cmnd[0] == 0x0a) || (workreq->cmnd[0] == 0x2a)) {
			outb((inb(tmport) & 0xf3) | 0x08, tmport);
		} else {
			outb(inb(tmport) & 0xf3, tmport);
		}		
	}	
	tmport = workport + 0x1c;

	if(workreq->sc_data_direction == DMA_TO_DEVICE) {
		dev->id[c][target_id].dirct = 0x20;
		if (inb(tmport) == 0) {
			tmport = workport + 0x18;
			outb(0x08, tmport);
			outb(0x01, tmpcip);
#ifdef ED_DBGP		
		printk( "start DMA(to target)\n");
#endif				
		} else {
			dev->last_cmd[c] |= 0x40;
		}
		dev->in_snd[c] = 0;
		return;
	}
	if (inb(tmport) == 0) {		
		tmport = workport + 0x18;
		outb(0x08, tmport);
		outb(0x09, tmpcip);
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
	unsigned int tmport;
	unsigned short int i, k;
	unsigned char j;

	tmport = dev->ioport[0] + 0x1c;
	outw(*val, tmport);
FUN_D7:
	for (i = 0; i < 10; i++) {	/* stable >= bus settle delay(400 ns)  */
		k = inw(tmport);
		j = (unsigned char) (k >> 8);
		if ((k & 0x8000) != 0) {	/* DB7 all release?    */
			goto FUN_D7;
		}
	}
	*val |= 0x4000;		/* assert DB6           */
	outw(*val, tmport);
	*val &= 0xdfff;		/* assert DB5           */
	outw(*val, tmport);
FUN_D5:
	for (i = 0; i < 10; i++) {	/* stable >= bus settle delay(400 ns) */
		if ((inw(tmport) & 0x2000) != 0) {	/* DB5 all release?       */
			goto FUN_D5;
		}
	}
	*val |= 0x8000;		/* no DB4-0, assert DB7    */
	*val &= 0xe0ff;
	outw(*val, tmport);
	*val &= 0xbfff;		/* release DB6             */
	outw(*val, tmport);
FUN_D6:
	for (i = 0; i < 10; i++) {	/* stable >= bus settle delay(400 ns)  */
		if ((inw(tmport) & 0x4000) != 0) {	/* DB6 all release?  */
			goto FUN_D6;
		}
	}

	return j;
}

static void tscam(struct Scsi_Host *host)
{

	unsigned int tmport;
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

	tmport = dev->ioport[0] + 1;
	outb(0x08, tmport++);
	outb(0x7f, tmport);
	tmport = dev->ioport[0] + 0x11;
	outb(0x20, tmport);

	if ((dev->scam_on & 0x40) == 0) {
		return;
	}
	m = 1;
	m <<= dev->host_id[0];
	j = 16;
	if (dev->chip_ver < 4) {
		m |= 0xff00;
		j = 8;
	}
	assignid_map = m;
	tmport = dev->ioport[0] + 0x02;
	outb(0x02, tmport++);	/* 2*2=4ms,3EH 2/32*3E=3.9ms */
	outb(0, tmport++);
	outb(0, tmport++);
	outb(0, tmport++);
	outb(0, tmport++);
	outb(0, tmport++);
	outb(0, tmport++);

	for (i = 0; i < j; i++) {
		m = 1;
		m = m << i;
		if ((m & assignid_map) != 0) {
			continue;
		}
		tmport = dev->ioport[0] + 0x0f;
		outb(0, tmport++);
		tmport += 0x02;
		outb(0, tmport++);
		outb(0, tmport++);
		outb(0, tmport++);
		if (i > 7) {
			k = (i & 0x07) | 0x40;
		} else {
			k = i;
		}
		outb(k, tmport++);
		tmport = dev->ioport[0] + 0x1b;
		if (dev->chip_ver == 4) {
			outb(0x01, tmport);
		} else {
			outb(0x00, tmport);
		}
wait_rdyok:
		tmport = dev->ioport[0] + 0x18;
		outb(0x09, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		k = inb(tmport);
		if (k != 0x16) {
			if ((k == 0x85) || (k == 0x42)) {
				continue;
			}
			tmport = dev->ioport[0] + 0x10;
			outb(0x41, tmport);
			goto wait_rdyok;
		}
		assignid_map |= m;

	}
	tmport = dev->ioport[0] + 0x02;
	outb(0x7f, tmport);
	tmport = dev->ioport[0] + 0x1b;
	outb(0x02, tmport);

	outb(0, 0x80);

	val = 0x0080;		/* bsy  */
	tmport = dev->ioport[0] + 0x1c;
	outw(val, tmport);
	val |= 0x0040;		/* sel  */
	outw(val, tmport);
	val |= 0x0004;		/* msg  */
	outw(val, tmport);
	inb(0x80);		/* 2 deskew delay(45ns*2=90ns) */
	val &= 0x007f;		/* no bsy  */
	outw(val, tmport);
	mdelay(128);
	val &= 0x00fb;		/* after 1ms no msg */
	outw(val, tmport);
wait_nomsg:
	if ((inb(tmport) & 0x04) != 0) {
		goto wait_nomsg;
	}
	outb(1, 0x80);
	udelay(100);
	for (n = 0; n < 0x30000; n++) {
		if ((inb(tmport) & 0x80) != 0) {	/* bsy ? */
			goto wait_io;
		}
	}
	goto TCM_SYNC;
wait_io:
	for (n = 0; n < 0x30000; n++) {
		if ((inb(tmport) & 0x81) == 0x0081) {
			goto wait_io1;
		}
	}
	goto TCM_SYNC;
wait_io1:
	inb(0x80);
	val |= 0x8003;		/* io,cd,db7  */
	outw(val, tmport);
	inb(0x80);
	val &= 0x00bf;		/* no sel     */
	outw(val, tmport);
	outb(2, 0x80);
TCM_SYNC:
	udelay(0x800);
	if ((inb(tmport) & 0x80) == 0x00) {	/* bsy ? */
		outw(0, tmport--);
		outb(0, tmport);
		tmport = dev->ioport[0] + 0x15;
		outb(0, tmport);
		tmport += 0x03;
		outb(0x09, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0)
			cpu_relax();
		tmport -= 0x08;
		inb(tmport);
		return;
	}
	val &= 0x00ff;		/* synchronization  */
	val |= 0x3f00;
	fun_scam(dev, &val);
	outb(3, 0x80);
	val &= 0x00ff;		/* isolation        */
	val |= 0x2000;
	fun_scam(dev, &val);
	outb(4, 0x80);
	i = 8;
	j = 0;
TCM_ID:
	if ((inw(tmport) & 0x2000) == 0) {
		goto TCM_ID;
	}
	outb(5, 0x80);
	val &= 0x00ff;		/* get ID_STRING */
	val |= 0x2000;
	k = fun_scam(dev, &val);
	if ((k & 0x03) == 0) {
		goto TCM_5;
	}
	mbuf[j] <<= 0x01;
	mbuf[j] &= 0xfe;
	if ((k & 0x02) != 0) {
		mbuf[j] |= 0x01;
	}
	i--;
	if (i > 0) {
		goto TCM_ID;
	}
	j++;
	i = 8;
	goto TCM_ID;

TCM_5:			/* isolation complete..  */
/*    mbuf[32]=0;
	printk(" \n%x %x %x %s\n ",assignid_map,mbuf[0],mbuf[1],&mbuf[2]); */
	i = 15;
	j = mbuf[0];
	if ((j & 0x20) != 0) {	/* bit5=1:ID upto 7      */
		i = 7;
	}
	if ((j & 0x06) == 0) {	/* IDvalid?             */
		goto G2Q5;
	}
	k = mbuf[1];
small_id:
	m = 1;
	m <<= k;
	if ((m & assignid_map) == 0) {
		goto G2Q_QUIN;
	}
	if (k > 0) {
		k--;
		goto small_id;
	}
G2Q5:			/* srch from max acceptable ID#  */
	k = i;			/* max acceptable ID#            */
G2Q_LP:
	m = 1;
	m <<= k;
	if ((m & assignid_map) == 0) {
		goto G2Q_QUIN;
	}
	if (k > 0) {
		k--;
		goto G2Q_LP;
	}
G2Q_QUIN:		/* k=binID#,       */
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

	goto TCM_SYNC;

}

static void is870(struct atp_unit *dev, unsigned int wkport)
{
	unsigned int tmport;
	unsigned char i, j, k, rmb, n;
	unsigned short int m;
	static unsigned char mbuf[512];
	static unsigned char satn[9] = { 0, 0, 0, 0, 0, 0, 0, 6, 6 };
	static unsigned char inqd[9] = { 0x12, 0, 0, 0, 0x24, 0, 0, 0x24, 6 };
	static unsigned char synn[6] = { 0x80, 1, 3, 1, 0x19, 0x0e };
	static unsigned char synu[6] = { 0x80, 1, 3, 1, 0x0c, 0x0e };
	static unsigned char synw[6] = { 0x80, 1, 3, 1, 0x0c, 0x07 };
	static unsigned char wide[6] = { 0x80, 1, 2, 3, 1, 0 };
	
	tmport = wkport + 0x3a;
	outb((unsigned char) (inb(tmport) | 0x10), tmport);

	for (i = 0; i < 16; i++) {
		if ((dev->chip_ver != 4) && (i > 7)) {
			break;
		}
		m = 1;
		m = m << i;
		if ((m & dev->active_id[0]) != 0) {
			continue;
		}
		if (i == dev->host_id[0]) {
			printk(KERN_INFO "         ID: %2d  Host Adapter\n", dev->host_id[0]);
			continue;
		}
		tmport = wkport + 0x1b;
		if (dev->chip_ver == 4) {
			outb(0x01, tmport);
		} else {
			outb(0x00, tmport);
		}
		tmport = wkport + 1;
		outb(0x08, tmport++);
		outb(0x7f, tmport++);
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[0][i].devsp, tmport++);
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		j = i;
		if ((j & 0x08) != 0) {
			j = (j & 0x07) | 0x40;
		}
		outb(j, tmport);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();

		tmport -= 0x08;
		if (inb(tmport) != 0x11 && inb(tmport) != 0x8e)
			continue;

		while (inb(tmport) != 0x8e)
			cpu_relax();

		dev->active_id[0] |= m;

		tmport = wkport + 0x10;
		outb(0x30, tmport);
		tmport = wkport + 0x04;
		outb(0x00, tmport);

phase_cmd:
		tmport = wkport + 0x18;
		outb(0x08, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		j = inb(tmport);
		if (j != 0x16) {
			tmport = wkport + 0x10;
			outb(0x41, tmport);
			goto phase_cmd;
		}
sel_ok:
		tmport = wkport + 3;
		outb(inqd[0], tmport++);
		outb(inqd[1], tmport++);
		outb(inqd[2], tmport++);
		outb(inqd[3], tmport++);
		outb(inqd[4], tmport++);
		outb(inqd[5], tmport);
		tmport += 0x07;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[0][i].devsp, tmport++);
		outb(0, tmport++);
		outb(inqd[6], tmport++);
		outb(inqd[7], tmport++);
		tmport += 0x03;
		outb(inqd[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		tmport -= 0x08;
		if (inb(tmport) != 0x11 && inb(tmport) != 0x8e)
			continue;

		while (inb(tmport) != 0x8e)
			cpu_relax();
			
		tmport = wkport + 0x1b;
		if (dev->chip_ver == 4)
			outb(0x00, tmport);

		tmport = wkport + 0x18;
		outb(0x08, tmport);
		tmport += 0x07;
		j = 0;
rd_inq_data:
		k = inb(tmport);
		if ((k & 0x01) != 0) {
			tmport -= 0x06;
			mbuf[j++] = inb(tmport);
			tmport += 0x06;
			goto rd_inq_data;
		}
		if ((k & 0x80) == 0) {
			goto rd_inq_data;
		}
		tmport -= 0x08;
		j = inb(tmport);
		if (j == 0x16) {
			goto inq_ok;
		}
		tmport = wkport + 0x10;
		outb(0x46, tmport);
		tmport += 0x02;
		outb(0, tmport++);
		outb(0, tmport++);
		outb(0, tmport++);
		tmport += 0x03;
		outb(0x08, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		tmport -= 0x08;
		if (inb(tmport) != 0x16) {
			goto sel_ok;
		}
inq_ok:
		mbuf[36] = 0;
		printk(KERN_INFO "         ID: %2d  %s\n", i, &mbuf[8]);
		dev->id[0][i].devtype = mbuf[0];
		rmb = mbuf[1];
		n = mbuf[7];
		if (dev->chip_ver != 4) {
			goto not_wide;
		}
		if ((mbuf[7] & 0x60) == 0) {
			goto not_wide;
		}
		if ((dev->global_map[0] & 0x20) == 0) {
			goto not_wide;
		}
		tmport = wkport + 0x1b;
		outb(0x01, tmport);
		tmport = wkport + 3;
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[0][i].devsp, tmport++);
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		tmport -= 0x08;
		if (inb(tmport) != 0x11 && inb(tmport) != 0x8e)
			continue;

		while (inb(tmport) != 0x8e)
			cpu_relax();
			
try_wide:
		j = 0;
		tmport = wkport + 0x14;
		outb(0x05, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				outb(wide[j++], tmport);
				tmport += 0x06;
			}
		}
		tmport -= 0x08;
		
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x18;
		outb(0x20, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				outb(0, tmport);
				tmport += 0x06;
			}
		}
		tmport -= 0x08;
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x14;
		outb(0xff, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;
		k = 0;
widep_in1:
		j = inb(tmport);
		if ((j & 0x01) != 0) {
			tmport -= 0x06;
			mbuf[k++] = inb(tmport);
			tmport += 0x06;
			goto widep_in1;
		}
		if ((j & 0x80) == 0x00) {
			goto widep_in1;
		}
		tmport -= 0x08;
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x10;
		outb(0x30, tmport);
		tmport = wkport + 0x14;
		outb(0x00, tmport);
		tmport += 0x04;
		outb(0x08, tmport);
		tmport += 0x07;
		
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();

		tmport -= 0x08;
		j = inb(tmport);
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
		dev->wide_id[0] |= m;
not_wide:
		if ((dev->id[0][i].devtype == 0x00) || (dev->id[0][i].devtype == 0x07) || ((dev->id[0][i].devtype == 0x05) && ((n & 0x10) != 0))) {
			goto set_sync;
		}
		continue;
set_sync:
		tmport = wkport + 0x1b;
		j = 0;
		if ((m & dev->wide_id[0]) != 0) {
			j |= 0x01;
		}
		outb(j, tmport);
		tmport = wkport + 3;
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[0][i].devsp, tmport++);
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		tmport -= 0x08;
		if (inb(tmport) != 0x11 && inb(tmport) != 0x8e)
			continue;

		while (inb(tmport) != 0x8e)
			cpu_relax();
			
try_sync:
		j = 0;
		tmport = wkport + 0x14;
		outb(0x06, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				if ((m & dev->wide_id[0]) != 0) {
					outb(synw[j++], tmport);
				} else {
					if ((m & dev->ultra_map[0]) != 0) {
						outb(synu[j++], tmport);
					} else {
						outb(synn[j++], tmport);
					}
				}
				tmport += 0x06;
			}
		}
		tmport -= 0x08;
		
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x18;
		outb(0x20, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00) {
			if ((inb(tmport) & 0x01) != 0x00) {
				tmport -= 0x06;
				outb(0x00, tmport);
				tmport += 0x06;
			}
		}
		tmport -= 0x08;
		j = inb(tmport);
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
		tmport = wkport + 0x14;
		outb(0xff, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;
		k = 0;
phase_ins1:
		j = inb(tmport);
		if ((j & 0x01) != 0x00) {
			tmport -= 0x06;
			mbuf[k++] = inb(tmport);
			tmport += 0x06;
			goto phase_ins1;
		}
		if ((j & 0x80) == 0x00) {
			goto phase_ins1;
		}
		tmport -= 0x08;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		j = inb(tmport);
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
		tmport = wkport + 0x10;
		outb(0x30, tmport);
tar_dcons:
		tmport = wkport + 0x14;
		outb(0x00, tmport);
		tmport += 0x04;
		outb(0x08, tmport);
		tmport += 0x07;
		
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		tmport -= 0x08;
		j = inb(tmport);
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
		if (mbuf[4] > 0x0c) {
			mbuf[4] = 0x0c;
		}
		dev->id[0][i].devsp = mbuf[4];
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
		dev->id[0][i].devsp = (dev->id[0][i].devsp & 0x0f) | j;
	}
	tmport = wkport + 0x3a;
	outb((unsigned char) (inb(tmport) & 0xef), tmport);
}

static void is880(struct atp_unit *dev, unsigned int wkport)
{
	unsigned int tmport;
	unsigned char i, j, k, rmb, n, lvdmode;
	unsigned short int m;
	static unsigned char mbuf[512];
	static unsigned char satn[9] = { 0, 0, 0, 0, 0, 0, 0, 6, 6 };
	static unsigned char inqd[9] = { 0x12, 0, 0, 0, 0x24, 0, 0, 0x24, 6 };
	static unsigned char synn[6] = { 0x80, 1, 3, 1, 0x19, 0x0e };
	unsigned char synu[6] = { 0x80, 1, 3, 1, 0x0a, 0x0e };
	static unsigned char synw[6] = { 0x80, 1, 3, 1, 0x19, 0x0e };
	unsigned char synuw[6] = { 0x80, 1, 3, 1, 0x0a, 0x0e };
	static unsigned char wide[6] = { 0x80, 1, 2, 3, 1, 0 };
	static unsigned char u3[9] = { 0x80, 1, 6, 4, 0x09, 00, 0x0e, 0x01, 0x02 };

	lvdmode = inb(wkport + 0x3f) & 0x40;

	for (i = 0; i < 16; i++) {
		m = 1;
		m = m << i;
		if ((m & dev->active_id[0]) != 0) {
			continue;
		}
		if (i == dev->host_id[0]) {
			printk(KERN_INFO "         ID: %2d  Host Adapter\n", dev->host_id[0]);
			continue;
		}
		tmport = wkport + 0x5b;
		outb(0x01, tmport);
		tmport = wkport + 0x41;
		outb(0x08, tmport++);
		outb(0x7f, tmport++);
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[0][i].devsp, tmport++);
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		j = i;
		if ((j & 0x08) != 0) {
			j = (j & 0x07) | 0x40;
		}
		outb(j, tmport);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();

		tmport -= 0x08;
		if (inb(tmport) != 0x11 && inb(tmport) != 0x8e)
			continue;

		while (inb(tmport) != 0x8e)
			cpu_relax();
			
		dev->active_id[0] |= m;

		tmport = wkport + 0x50;
		outb(0x30, tmport);
		tmport = wkport + 0x54;
		outb(0x00, tmport);

phase_cmd:
		tmport = wkport + 0x58;
		outb(0x08, tmport);
		tmport += 0x07;
		
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();

		tmport -= 0x08;
		j = inb(tmport);
		if (j != 0x16) {
			tmport = wkport + 0x50;
			outb(0x41, tmport);
			goto phase_cmd;
		}
sel_ok:
		tmport = wkport + 0x43;
		outb(inqd[0], tmport++);
		outb(inqd[1], tmport++);
		outb(inqd[2], tmport++);
		outb(inqd[3], tmport++);
		outb(inqd[4], tmport++);
		outb(inqd[5], tmport);
		tmport += 0x07;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[0][i].devsp, tmport++);
		outb(0, tmport++);
		outb(inqd[6], tmport++);
		outb(inqd[7], tmport++);
		tmport += 0x03;
		outb(inqd[8], tmport);
		tmport += 0x07;
		
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		tmport -= 0x08;
		if (inb(tmport) != 0x11 && inb(tmport) != 0x8e)
			continue;

		while (inb(tmport) != 0x8e)
			cpu_relax();
			
		tmport = wkport + 0x5b;
		outb(0x00, tmport);
		tmport = wkport + 0x58;
		outb(0x08, tmport);
		tmport += 0x07;
		j = 0;
rd_inq_data:
		k = inb(tmport);
		if ((k & 0x01) != 0) {
			tmport -= 0x06;
			mbuf[j++] = inb(tmport);
			tmport += 0x06;
			goto rd_inq_data;
		}
		if ((k & 0x80) == 0) {
			goto rd_inq_data;
		}
		tmport -= 0x08;
		j = inb(tmport);
		if (j == 0x16) {
			goto inq_ok;
		}
		tmport = wkport + 0x50;
		outb(0x46, tmport);
		tmport += 0x02;
		outb(0, tmport++);
		outb(0, tmport++);
		outb(0, tmport++);
		tmport += 0x03;
		outb(0x08, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		tmport -= 0x08;
		if (inb(tmport) != 0x16)
			goto sel_ok;

inq_ok:
		mbuf[36] = 0;
		printk(KERN_INFO "         ID: %2d  %s\n", i, &mbuf[8]);
		dev->id[0][i].devtype = mbuf[0];
		rmb = mbuf[1];
		n = mbuf[7];
		if ((mbuf[7] & 0x60) == 0) {
			goto not_wide;
		}
		if ((i < 8) && ((dev->global_map[0] & 0x20) == 0)) {
			goto not_wide;
		}
		if (lvdmode == 0) {
			goto chg_wide;
		}
		if (dev->sp[0][i] != 0x04)	// force u2
		{
			goto chg_wide;
		}

		tmport = wkport + 0x5b;
		outb(0x01, tmport);
		tmport = wkport + 0x43;
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[0][i].devsp, tmport++);
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();

		tmport -= 0x08;

		if (inb(tmport) != 0x11 && inb(tmport) != 0x8e)
			continue;

		while (inb(tmport) != 0x8e)
			cpu_relax();

try_u3:
		j = 0;
		tmport = wkport + 0x54;
		outb(0x09, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				outb(u3[j++], tmport);
				tmport += 0x06;
			}
		}
		tmport -= 0x08;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x58;
		outb(0x20, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				outb(0, tmport);
				tmport += 0x06;
			}
		}
		tmport -= 0x08;
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x54;
		outb(0x09, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;
		k = 0;
u3p_in1:
		j = inb(tmport);
		if ((j & 0x01) != 0) {
			tmport -= 0x06;
			mbuf[k++] = inb(tmport);
			tmport += 0x06;
			goto u3p_in1;
		}
		if ((j & 0x80) == 0x00) {
			goto u3p_in1;
		}
		tmport -= 0x08;
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x50;
		outb(0x30, tmport);
		tmport = wkport + 0x54;
		outb(0x00, tmport);
		tmport += 0x04;
		outb(0x08, tmport);
		tmport += 0x07;
		
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		tmport -= 0x08;
		j = inb(tmport);
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
			dev->wide_id[0] |= m;
			dev->id[0][i].devsp = 0xce;
			continue;
		}
chg_wide:
		tmport = wkport + 0x5b;
		outb(0x01, tmport);
		tmport = wkport + 0x43;
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[0][i].devsp, tmport++);
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		tmport -= 0x08;
		if (inb(tmport) != 0x11 && inb(tmport) != 0x8e)
			continue;

		while (inb(tmport) != 0x8e)
			cpu_relax();
			
try_wide:
		j = 0;
		tmport = wkport + 0x54;
		outb(0x05, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				outb(wide[j++], tmport);
				tmport += 0x06;
			}
		}
		tmport -= 0x08;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
			
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x58;
		outb(0x20, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				outb(0, tmport);
				tmport += 0x06;
			}
		}
		tmport -= 0x08;
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x54;
		outb(0xff, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;
		k = 0;
widep_in1:
		j = inb(tmport);
		if ((j & 0x01) != 0) {
			tmport -= 0x06;
			mbuf[k++] = inb(tmport);
			tmport += 0x06;
			goto widep_in1;
		}
		if ((j & 0x80) == 0x00) {
			goto widep_in1;
		}
		tmport -= 0x08;
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x50;
		outb(0x30, tmport);
		tmport = wkport + 0x54;
		outb(0x00, tmport);
		tmport += 0x04;
		outb(0x08, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();

		tmport -= 0x08;
		j = inb(tmport);
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
		dev->wide_id[0] |= m;
not_wide:
		if ((dev->id[0][i].devtype == 0x00) || (dev->id[0][i].devtype == 0x07) || ((dev->id[0][i].devtype == 0x05) && ((n & 0x10) != 0))) {
			m = 1;
			m = m << i;
			if ((dev->async[0] & m) != 0) {
				goto set_sync;
			}
		}
		continue;
set_sync:
		if (dev->sp[0][i] == 0x02) {
			synu[4] = 0x0c;
			synuw[4] = 0x0c;
		} else {
			if (dev->sp[0][i] >= 0x03) {
				synu[4] = 0x0a;
				synuw[4] = 0x0a;
			}
		}
		tmport = wkport + 0x5b;
		j = 0;
		if ((m & dev->wide_id[0]) != 0) {
			j |= 0x01;
		}
		outb(j, tmport);
		tmport = wkport + 0x43;
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[0][i].devsp, tmport++);
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();

		tmport -= 0x08;
		if ((inb(tmport) != 0x11) && (inb(tmport) != 0x8e)) {
			continue;
		}
		while (inb(tmport) != 0x8e)
			cpu_relax();

try_sync:
		j = 0;
		tmport = wkport + 0x54;
		outb(0x06, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				if ((m & dev->wide_id[0]) != 0) {
					if ((m & dev->ultra_map[0]) != 0) {
						outb(synuw[j++], tmport);
					} else {
						outb(synw[j++], tmport);
					}
				} else {
					if ((m & dev->ultra_map[0]) != 0) {
						outb(synu[j++], tmport);
					} else {
						outb(synn[j++], tmport);
					}
				}
				tmport += 0x06;
			}
		}
		tmport -= 0x08;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();

		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x58;
		outb(0x20, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00) {
			if ((inb(tmport) & 0x01) != 0x00) {
				tmport -= 0x06;
				outb(0x00, tmport);
				tmport += 0x06;
			}
		}
		tmport -= 0x08;
		j = inb(tmport);
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
		tmport = wkport + 0x54;
		outb(0x06, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;
		k = 0;
phase_ins1:
		j = inb(tmport);
		if ((j & 0x01) != 0x00) {
			tmport -= 0x06;
			mbuf[k++] = inb(tmport);
			tmport += 0x06;
			goto phase_ins1;
		}
		if ((j & 0x80) == 0x00) {
			goto phase_ins1;
		}
		tmport -= 0x08;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();

		j = inb(tmport);
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
		tmport = wkport + 0x50;
		outb(0x30, tmport);
tar_dcons:
		tmport = wkport + 0x54;
		outb(0x00, tmport);
		tmport += 0x04;
		outb(0x08, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();

		tmport -= 0x08;
		j = inb(tmport);
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
		if (mbuf[4] > 0x0e) {
			mbuf[4] = 0x0e;
		}
		dev->id[0][i].devsp = mbuf[4];
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
		dev->id[0][i].devsp = (dev->id[0][i].devsp & 0x0f) | j;
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
			pci_free_consistent(atp_dev->pdev, 1024, atp_dev->id[j][k].prd_table, atp_dev->id[j][k].prd_bus);
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
	   			atp_dev->id[c][k].prd_table = pci_alloc_consistent(atp_dev->pdev, 1024, &(atp_dev->id[c][k].prd_bus));
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

/* return non-zero on detection */
static int atp870u_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	unsigned char k, m, c;
	unsigned long flags;
	unsigned int base_io, tmport, error,n;
	unsigned char host_id;
	struct Scsi_Host *shpnt = NULL;
	struct atp_unit atp_dev, *p;
	unsigned char setupdata[2][16];
	int count = 0;
	
	if (pci_enable_device(pdev))
		return -EIO;

        if (!pci_set_dma_mask(pdev, 0xFFFFFFFFUL)) {
                printk(KERN_INFO "atp870u: use 32bit DMA mask.\n");
        } else {
                printk(KERN_ERR "atp870u: DMA mask required but not available.\n");
                return -EIO;
        }

	memset(&atp_dev, 0, sizeof atp_dev);
	/*
	 * It's probably easier to weed out some revisions like
	 * this than via the PCI device table
	 */
	if (ent->device == PCI_DEVICE_ID_ARTOP_AEC7610) {
		error = pci_read_config_byte(pdev, PCI_CLASS_REVISION, &atp_dev.chip_ver);
		if (atp_dev.chip_ver < 2)
			return -EIO;
	}

	switch (ent->device) {
	case PCI_DEVICE_ID_ARTOP_AEC7612UW:
	case PCI_DEVICE_ID_ARTOP_AEC7612SUW:
	case ATP880_DEVID1:	
	case ATP880_DEVID2:	
	case ATP885_DEVID:	
		atp_dev.chip_ver = 0x04;
	default:
		break;
	}
	base_io = pci_resource_start(pdev, 0);
	base_io &= 0xfffffff8;
	
	if ((ent->device == ATP880_DEVID1)||(ent->device == ATP880_DEVID2)) {
		error = pci_read_config_byte(pdev, PCI_CLASS_REVISION, &atp_dev.chip_ver);
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x80);//JCC082803

		host_id = inb(base_io + 0x39);
		host_id >>= 0x04;

		printk(KERN_INFO "   ACARD AEC-67160 PCI Ultra3 LVD Host Adapter: %d"
			"    IO:%x, IRQ:%d.\n", count, base_io, pdev->irq);
		atp_dev.ioport[0] = base_io + 0x40;
		atp_dev.pciport[0] = base_io + 0x28;
		atp_dev.dev_id = ent->device;
		atp_dev.host_id[0] = host_id;

		tmport = base_io + 0x22;
		atp_dev.scam_on = inb(tmport);
		tmport += 0x13;
		atp_dev.global_map[0] = inb(tmport);
		tmport += 0x07;
		atp_dev.ultra_map[0] = inw(tmport);

		n = 0x3f09;
next_fblk_880:
		if (n >= 0x4000)
			goto flash_ok_880;

		m = 0;
		outw(n, base_io + 0x34);
		n += 0x0002;
		if (inb(base_io + 0x30) == 0xff)
			goto flash_ok_880;

		atp_dev.sp[0][m++] = inb(base_io + 0x30);
		atp_dev.sp[0][m++] = inb(base_io + 0x31);
		atp_dev.sp[0][m++] = inb(base_io + 0x32);
		atp_dev.sp[0][m++] = inb(base_io + 0x33);
		outw(n, base_io + 0x34);
		n += 0x0002;
		atp_dev.sp[0][m++] = inb(base_io + 0x30);
		atp_dev.sp[0][m++] = inb(base_io + 0x31);
		atp_dev.sp[0][m++] = inb(base_io + 0x32);
		atp_dev.sp[0][m++] = inb(base_io + 0x33);
		outw(n, base_io + 0x34);
		n += 0x0002;
		atp_dev.sp[0][m++] = inb(base_io + 0x30);
		atp_dev.sp[0][m++] = inb(base_io + 0x31);
		atp_dev.sp[0][m++] = inb(base_io + 0x32);
		atp_dev.sp[0][m++] = inb(base_io + 0x33);
		outw(n, base_io + 0x34);
		n += 0x0002;
		atp_dev.sp[0][m++] = inb(base_io + 0x30);
		atp_dev.sp[0][m++] = inb(base_io + 0x31);
		atp_dev.sp[0][m++] = inb(base_io + 0x32);
		atp_dev.sp[0][m++] = inb(base_io + 0x33);
		n += 0x0018;
		goto next_fblk_880;
flash_ok_880:
		outw(0, base_io + 0x34);
		atp_dev.ultra_map[0] = 0;
		atp_dev.async[0] = 0;
		for (k = 0; k < 16; k++) {
			n = 1;
			n = n << k;
			if (atp_dev.sp[0][k] > 1) {
				atp_dev.ultra_map[0] |= n;
			} else {
				if (atp_dev.sp[0][k] == 0)
					atp_dev.async[0] |= n;
 			}
	 	}
		atp_dev.async[0] = ~(atp_dev.async[0]);
		outb(atp_dev.global_map[0], base_io + 0x35);
 
		shpnt = scsi_host_alloc(&atp870u_template, sizeof(struct atp_unit));
		if (!shpnt)
			return -ENOMEM;

		p = (struct atp_unit *)&shpnt->hostdata;

		atp_dev.host = shpnt;
		atp_dev.pdev = pdev;
		pci_set_drvdata(pdev, p);
		memcpy(p, &atp_dev, sizeof atp_dev);
		if (atp870u_init_tables(shpnt) < 0) {
			printk(KERN_ERR "Unable to allocate tables for Acard controller\n");
			goto unregister;
		}

		if (request_irq(pdev->irq, atp870u_intr_handle, SA_SHIRQ, "atp880i", shpnt)) {
 			printk(KERN_ERR "Unable to allocate IRQ%d for Acard controller.\n", pdev->irq);
			goto free_tables;
		}

		spin_lock_irqsave(shpnt->host_lock, flags);
		tmport = base_io + 0x38;
		k = inb(tmport) & 0x80;
		outb(k, tmport);
		tmport += 0x03;
		outb(0x20, tmport);
		mdelay(32);
		outb(0, tmport);
		mdelay(32);
		tmport = base_io + 0x5b;
		inb(tmport);
		tmport -= 0x04;
		inb(tmport);
		tmport = base_io + 0x40;
		outb((host_id | 0x08), tmport);
		tmport += 0x18;
		outb(0, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0)
			mdelay(1);
		tmport -= 0x08;
		inb(tmport);
		tmport = base_io + 0x41;
		outb(8, tmport++);
		outb(0x7f, tmport);
		tmport = base_io + 0x51;
		outb(0x20, tmport);

		tscam(shpnt);
		is880(p, base_io);
		tmport = base_io + 0x38;
		outb(0xb0, tmport);
		shpnt->max_id = 16;
		shpnt->this_id = host_id;
		shpnt->unique_id = base_io;
		shpnt->io_port = base_io;
		shpnt->n_io_port = 0x60;	/* Number of bytes of I/O space used */
		shpnt->irq = pdev->irq;			
	} else if (ent->device == ATP885_DEVID) {	
			printk(KERN_INFO "   ACARD AEC-67162 PCI Ultra3 LVD Host Adapter:  IO:%x, IRQ:%d.\n"
			       , base_io, pdev->irq);
        	
		atp_dev.pdev = pdev;	
		atp_dev.dev_id  = ent->device;
		atp_dev.baseport = base_io;
		atp_dev.ioport[0] = base_io + 0x80;
		atp_dev.ioport[1] = base_io + 0xc0;
		atp_dev.pciport[0] = base_io + 0x40;
		atp_dev.pciport[1] = base_io + 0x50;
				
		shpnt = scsi_host_alloc(&atp870u_template, sizeof(struct atp_unit));
		if (!shpnt)
			return -ENOMEM;
        	
		p = (struct atp_unit *)&shpnt->hostdata;
        	
		atp_dev.host = shpnt;
		atp_dev.pdev = pdev;
		pci_set_drvdata(pdev, p);
		memcpy(p, &atp_dev, sizeof(struct atp_unit));
		if (atp870u_init_tables(shpnt) < 0)
			goto unregister;
			
#ifdef ED_DBGP		
	printk("request_irq() shpnt %p hostdata %p\n", shpnt, p);
#endif	        
		if (request_irq(pdev->irq, atp870u_intr_handle, SA_SHIRQ, "atp870u", shpnt)) {
				printk(KERN_ERR "Unable to allocate IRQ for Acard controller.\n");
			goto free_tables;
		}
		
		spin_lock_irqsave(shpnt->host_lock, flags);        					
        			
		c=inb(base_io + 0x29);
		outb((c | 0x04),base_io + 0x29);
        	
		n=0x1f80;
next_fblk_885:
		if (n >= 0x2000) {
		   goto flash_ok_885;
		}
		outw(n,base_io + 0x3c);
		if (inl(base_io + 0x38) == 0xffffffff) {
		   goto flash_ok_885;
		}
		for (m=0; m < 2; m++) {
		    p->global_map[m]= 0;
		    for (k=0; k < 4; k++) {
			outw(n++,base_io + 0x3c);
			((unsigned long *)&setupdata[m][0])[k]=inl(base_io + 0x38);
		    }
		    for (k=0; k < 4; k++) {
			outw(n++,base_io + 0x3c);
			((unsigned long *)&p->sp[m][0])[k]=inl(base_io + 0x38);
		    }
		    n += 8;
		}
		goto next_fblk_885;
flash_ok_885:
#ifdef ED_DBGP
		printk( "Flash Read OK\n");
#endif	
		c=inb(base_io + 0x29);
		outb((c & 0xfb),base_io + 0x29);
		for (c=0;c < 2;c++) {
		    p->ultra_map[c]=0;
		    p->async[c] = 0;
		    for (k=0; k < 16; k++) {
			n=1;
			n = n << k;
			if (p->sp[c][k] > 1) {
			   p->ultra_map[c] |= n;
			} else {
			   if (p->sp[c][k] == 0) {
			      p->async[c] |= n;
			   }
			}
		    }
		    p->async[c] = ~(p->async[c]);

		    if (p->global_map[c] == 0) {
		       k=setupdata[c][1];
		       if ((k & 0x40) != 0)
			  p->global_map[c] |= 0x20;
		       k &= 0x07;
		       p->global_map[c] |= k;
		       if ((setupdata[c][2] & 0x04) != 0)
			  p->global_map[c] |= 0x08;
		       p->host_id[c] = setupdata[c][0] & 0x07;
		    }
		}

		k = inb(base_io + 0x28) & 0x8f;
		k |= 0x10;
		outb(k, base_io + 0x28);
		outb(0x80, base_io + 0x41);
		outb(0x80, base_io + 0x51);
		mdelay(100);
		outb(0, base_io + 0x41);
		outb(0, base_io + 0x51);
		mdelay(1000);
		inb(base_io + 0x9b);
		inb(base_io + 0x97);
		inb(base_io + 0xdb);
		inb(base_io + 0xd7);
		tmport = base_io + 0x80;
		k=p->host_id[0];
		if (k > 7)
		   k = (k & 0x07) | 0x40;
		k |= 0x08;
		outb(k, tmport);
		tmport += 0x18;
		outb(0, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0)
			cpu_relax();
	
		tmport -= 0x08;
		inb(tmport);
		tmport = base_io + 0x81;
		outb(8, tmport++);
		outb(0x7f, tmport);
		tmport = base_io + 0x91;
		outb(0x20, tmport);

		tmport = base_io + 0xc0;
		k=p->host_id[1];
		if (k > 7)
		   k = (k & 0x07) | 0x40;
		k |= 0x08;
		outb(k, tmport);
		tmport += 0x18;
		outb(0, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0)
			cpu_relax();

		tmport -= 0x08;
		inb(tmport);
		tmport = base_io + 0xc1;
		outb(8, tmport++);
		outb(0x7f, tmport);
		tmport = base_io + 0xd1;
		outb(0x20, tmport);

		tscam_885();
		printk(KERN_INFO "   Scanning Channel A SCSI Device ...\n");
		is885(p, base_io + 0x80, 0);
		printk(KERN_INFO "   Scanning Channel B SCSI Device ...\n");
		is885(p, base_io + 0xc0, 1);

		k = inb(base_io + 0x28) & 0xcf;
		k |= 0xc0;
		outb(k, base_io + 0x28);
		k = inb(base_io + 0x1f) | 0x80;
		outb(k, base_io + 0x1f);
		k = inb(base_io + 0x29) | 0x01;
		outb(k, base_io + 0x29);
#ifdef ED_DBGP
		//printk("atp885: atp_host[0] 0x%p\n", atp_host[0]);
#endif		
		shpnt->max_id = 16;
		shpnt->max_lun = (p->global_map[0] & 0x07) + 1;
		shpnt->max_channel = 1;
		shpnt->this_id = p->host_id[0];
		shpnt->unique_id = base_io;
		shpnt->io_port = base_io;
		shpnt->n_io_port = 0xff;	/* Number of bytes of I/O space used */
		shpnt->irq = pdev->irq;
				
	} else {
		error = pci_read_config_byte(pdev, 0x49, &host_id);

		printk(KERN_INFO "   ACARD AEC-671X PCI Ultra/W SCSI-2/3 Host Adapter: %d "
			"IO:%x, IRQ:%d.\n", count, base_io, pdev->irq);

		atp_dev.ioport[0] = base_io;
		atp_dev.pciport[0] = base_io + 0x20;
		atp_dev.dev_id = ent->device;
		host_id &= 0x07;
		atp_dev.host_id[0] = host_id;
		tmport = base_io + 0x22;
		atp_dev.scam_on = inb(tmport);
		tmport += 0x0b;
		atp_dev.global_map[0] = inb(tmport++);
		atp_dev.ultra_map[0] = inw(tmport);

		if (atp_dev.ultra_map[0] == 0) {
			atp_dev.scam_on = 0x00;
			atp_dev.global_map[0] = 0x20;
			atp_dev.ultra_map[0] = 0xffff;
		}

		shpnt = scsi_host_alloc(&atp870u_template, sizeof(struct atp_unit));
		if (!shpnt)
			return -ENOMEM;

		p = (struct atp_unit *)&shpnt->hostdata;
		
		atp_dev.host = shpnt;
		atp_dev.pdev = pdev;
		pci_set_drvdata(pdev, p);
		memcpy(p, &atp_dev, sizeof atp_dev);
		if (atp870u_init_tables(shpnt) < 0)
			goto unregister;

		if (request_irq(pdev->irq, atp870u_intr_handle, SA_SHIRQ, "atp870i", shpnt)) {
			printk(KERN_ERR "Unable to allocate IRQ%d for Acard controller.\n", pdev->irq);
			goto free_tables;
		}

		spin_lock_irqsave(shpnt->host_lock, flags);
		if (atp_dev.chip_ver > 0x07) {	/* check if atp876 chip then enable terminator */
			tmport = base_io + 0x3e;
			outb(0x00, tmport);
		}
 
		tmport = base_io + 0x3a;
		k = (inb(tmport) & 0xf3) | 0x10;
		outb(k, tmport);
		outb((k & 0xdf), tmport);
		mdelay(32);
		outb(k, tmport);
		mdelay(32);
		tmport = base_io;
		outb((host_id | 0x08), tmport);
		tmport += 0x18;
		outb(0, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0)
			mdelay(1);

		tmport -= 0x08;
		inb(tmport);
		tmport = base_io + 1;
		outb(8, tmport++);
		outb(0x7f, tmport);
		tmport = base_io + 0x11;
		outb(0x20, tmport);

		tscam(shpnt);
		is870(p, base_io);
		tmport = base_io + 0x3a;
		outb((inb(tmport) & 0xef), tmport);
		tmport++;
		outb((inb(tmport) | 0x20), tmport);
		if (atp_dev.chip_ver == 4)
			shpnt->max_id = 16;
		else		
			shpnt->max_id = 7;
		shpnt->this_id = host_id;
		shpnt->unique_id = base_io;
		shpnt->io_port = base_io;
		shpnt->n_io_port = 0x40;	/* Number of bytes of I/O space used */
		shpnt->irq = pdev->irq;		
	} 
		spin_unlock_irqrestore(shpnt->host_lock, flags);
		if(ent->device==ATP885_DEVID) {
			if(!request_region(base_io, 0xff, "atp870u")) /* Register the IO ports that we use */
				goto request_io_fail;
		} else if((ent->device==ATP880_DEVID1)||(ent->device==ATP880_DEVID2)) {
			if(!request_region(base_io, 0x60, "atp870u")) /* Register the IO ports that we use */
				goto request_io_fail;
		} else {
			if(!request_region(base_io, 0x40, "atp870u")) /* Register the IO ports that we use */
				goto request_io_fail;
		}				
		count++;
		if (scsi_add_host(shpnt, &pdev->dev))
			goto scsi_add_fail;
		scsi_scan_host(shpnt);
#ifdef ED_DBGP			
		printk("atp870u_prob : exit\n");
#endif		
		return 0;

scsi_add_fail:
	printk("atp870u_prob:scsi_add_fail\n");
	if(ent->device==ATP885_DEVID) {
		release_region(base_io, 0xff);
	} else if((ent->device==ATP880_DEVID1)||(ent->device==ATP880_DEVID2)) {
		release_region(base_io, 0x60);
	} else {
		release_region(base_io, 0x40);
	}
request_io_fail:
	printk("atp870u_prob:request_io_fail\n");
	free_irq(pdev->irq, shpnt);
free_tables:
	printk("atp870u_prob:free_table\n");
	atp870u_free_tables(shpnt);
unregister:
	printk("atp870u_prob:unregister\n");
	scsi_host_put(shpnt);
	return -1;		
}

/* The abort command does not leave the device in a clean state where
   it is available to be used again.  Until this gets worked out, we will
   leave it commented out.  */

static int atp870u_abort(struct scsi_cmnd * SCpnt)
{
	unsigned char  j, k, c;
	struct scsi_cmnd *workrequ;
	unsigned int tmport;
	struct atp_unit *dev;	
	struct Scsi_Host *host;
	host = SCpnt->device->host;

	dev = (struct atp_unit *)&host->hostdata;
	c=SCpnt->device->channel;
	printk(" atp870u: abort Channel = %x \n", c);
	printk("working=%x last_cmd=%x ", dev->working[c], dev->last_cmd[c]);
	printk(" quhdu=%x quendu=%x ", dev->quhd[c], dev->quend[c]);
	tmport = dev->ioport[c];
	for (j = 0; j < 0x18; j++) {
		printk(" r%2x=%2x", j, inb(tmport++));
	}
	tmport += 0x04;
	printk(" r1c=%2x", inb(tmport));
	tmport += 0x03;
	printk(" r1f=%2x in_snd=%2x ", inb(tmport), dev->in_snd[c]);
	tmport= dev->pciport[c];
	printk(" d00=%2x", inb(tmport));
	tmport += 0x02;
	printk(" d02=%2x", inb(tmport));
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

#define BLS buffer + len + size
static int atp870u_proc_info(struct Scsi_Host *HBAptr, char *buffer, 
			     char **start, off_t offset, int length, int inout)
{
	static u8 buff[512];
	int size = 0;
	int len = 0;
	off_t begin = 0;
	off_t pos = 0;
	
	if (inout) 	
		return -EINVAL;
	if (offset == 0)
		memset(buff, 0, sizeof(buff));
	size += sprintf(BLS, "ACARD AEC-671X Driver Version: 2.6+ac\n");
	len += size;
	pos = begin + len;
	size = 0;

	size += sprintf(BLS, "\n");
	size += sprintf(BLS, "Adapter Configuration:\n");
	size += sprintf(BLS, "               Base IO: %#.4lx\n", HBAptr->io_port);
	size += sprintf(BLS, "                   IRQ: %d\n", HBAptr->irq);
	len += size;
	pos = begin + len;
	
	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);	/* Start slop */
	if (len > length) {
		len = length;	/* Ending slop */
	}
	return (len);
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
	printk(KERN_INFO "free_irq : %d\n",pshost->irq);
	free_irq(pshost->irq, pshost);
	release_region(pshost->io_port, pshost->n_io_port);
	printk(KERN_INFO "atp870u_free_tables : %p\n",pshost);
	atp870u_free_tables(pshost);
	printk(KERN_INFO "scsi_host_put : %p\n",pshost);
	scsi_host_put(pshost);
	printk(KERN_INFO "pci_set_drvdata : %p\n",pdev);
	pci_set_drvdata(pdev, NULL);	
}
MODULE_LICENSE("GPL");

static struct scsi_host_template atp870u_template = {
     .module			= THIS_MODULE,
     .name              	= "atp870u"		/* name */,
     .proc_name			= "atp870u",
     .proc_info			= atp870u_proc_info,
     .info              	= atp870u_info		/* info */,
     .queuecommand      	= atp870u_queuecommand	/* queuecommand */,
     .eh_abort_handler  	= atp870u_abort		/* abort */,
     .bios_param        	= atp870u_biosparam	/* biosparm */,
     .can_queue         	= qcnt			/* can_queue */,
     .this_id           	= 7			/* SCSI ID */,
     .sg_tablesize      	= ATP870U_SCATTER	/*SG_ALL*/ /*SG_NONE*/,
     .cmd_per_lun       	= ATP870U_CMDLUN		/* commands per lun */,
     .use_clustering    	= ENABLE_CLUSTERING,
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
	.remove		= __devexit_p(atp870u_remove),
};

static int __init atp870u_init(void)
{
#ifdef ED_DBGP	
	printk("atp870u_init: Entry\n");
#endif	
	return pci_register_driver(&atp870u_driver);
}

static void __exit atp870u_exit(void)
{
#ifdef ED_DBGP	
	printk("atp870u_exit: Entry\n");
#endif
	pci_unregister_driver(&atp870u_driver);
}

static void tscam_885(void)
{
	unsigned char i;

	for (i = 0; i < 0x2; i++) {
		mdelay(300);
	}
	return;
}



static void is885(struct atp_unit *dev, unsigned int wkport,unsigned char c)
{
	unsigned int tmport;
	unsigned char i, j, k, rmb, n, lvdmode;
	unsigned short int m;
	static unsigned char mbuf[512];
	static unsigned char satn[9] =	{0, 0, 0, 0, 0, 0, 0, 6, 6};
	static unsigned char inqd[9] =	{0x12, 0, 0, 0, 0x24, 0, 0, 0x24, 6};
	static unsigned char synn[6] =	{0x80, 1, 3, 1, 0x19, 0x0e};
	unsigned char synu[6] =  {0x80, 1, 3, 1, 0x0a, 0x0e};
	static unsigned char synw[6] =	{0x80, 1, 3, 1, 0x19, 0x0e};
	unsigned char synuw[6] =  {0x80, 1, 3, 1, 0x0a, 0x0e};
	static unsigned char wide[6] =	{0x80, 1, 2, 3, 1, 0};
	static unsigned char u3[9] = { 0x80,1,6,4,0x09,00,0x0e,0x01,0x02 };

	lvdmode=inb(wkport + 0x1b) >> 7;

	for (i = 0; i < 16; i++) {
		m = 1;
		m = m << i;
		if ((m & dev->active_id[c]) != 0) {
			continue;
		}
		if (i == dev->host_id[c]) {
			printk(KERN_INFO "         ID: %2d  Host Adapter\n", dev->host_id[c]);
			continue;
		}
		tmport = wkport + 0x1b;
		outb(0x01, tmport);
		tmport = wkport + 0x01;
		outb(0x08, tmport++);
		outb(0x7f, tmport++);
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[c][i].devsp, tmport++);
		
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		j = i;
		if ((j & 0x08) != 0) {
			j = (j & 0x07) | 0x40;
		}
		outb(j, tmport);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		if ((inb(tmport) != 0x11) && (inb(tmport) != 0x8e)) {
			continue;
		}
		while (inb(tmport) != 0x8e)
			cpu_relax();
		dev->active_id[c] |= m;

		tmport = wkport + 0x10;
		outb(0x30, tmport);
		tmport = wkport + 0x14;
		outb(0x00, tmport);

phase_cmd:
		tmport = wkport + 0x18;
		outb(0x08, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		j = inb(tmport);
		if (j != 0x16) {
			tmport = wkport + 0x10;
			outb(0x41, tmport);
			goto phase_cmd;
		}
sel_ok:
		tmport = wkport + 0x03;
		outb(inqd[0], tmport++);
		outb(inqd[1], tmport++);
		outb(inqd[2], tmport++);
		outb(inqd[3], tmport++);
		outb(inqd[4], tmport++);
		outb(inqd[5], tmport);
		tmport += 0x07;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[c][i].devsp, tmport++);
		outb(0, tmport++);
		outb(inqd[6], tmport++);
		outb(inqd[7], tmport++);
		tmport += 0x03;
		outb(inqd[8], tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		if ((inb(tmport) != 0x11) && (inb(tmport) != 0x8e)) {
			continue;
		}
		while (inb(tmport) != 0x8e)
			cpu_relax();
		tmport = wkport + 0x1b;
		outb(0x00, tmport);
		tmport = wkport + 0x18;
		outb(0x08, tmport);
		tmport += 0x07;
		j = 0;
rd_inq_data:
		k = inb(tmport);
		if ((k & 0x01) != 0) {
			tmport -= 0x06;
			mbuf[j++] = inb(tmport);
			tmport += 0x06;
			goto rd_inq_data;
		}
		if ((k & 0x80) == 0) {
			goto rd_inq_data;
		}
		tmport -= 0x08;
		j = inb(tmport);
		if (j == 0x16) {
			goto inq_ok;
		}
		tmport = wkport + 0x10;
		outb(0x46, tmport);
		tmport += 0x02;
		outb(0, tmport++);
		outb(0, tmport++);
		outb(0, tmport++);
		tmport += 0x03;
		outb(0x08, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		if (inb(tmport) != 0x16) {
			goto sel_ok;
		}
inq_ok:
		mbuf[36] = 0;
		printk( KERN_INFO"         ID: %2d  %s\n", i, &mbuf[8]);
		dev->id[c][i].devtype = mbuf[0];
		rmb = mbuf[1];
		n = mbuf[7];
		if ((mbuf[7] & 0x60) == 0) {
			goto not_wide;
		}
		if ((i < 8) && ((dev->global_map[c] & 0x20) == 0)) {
			goto not_wide;
		}
		if (lvdmode == 0) {
		   goto chg_wide;
		}
		if (dev->sp[c][i] != 0x04) {	// force u2
		   goto chg_wide;
		}

		tmport = wkport + 0x1b;
		outb(0x01, tmport);
		tmport = wkport + 0x03;
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[c][i].devsp, tmport++);
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		if ((inb(tmport) != 0x11) && (inb(tmport) != 0x8e)) {
			continue;
		}
		while (inb(tmport) != 0x8e)
			cpu_relax();
try_u3:
		j = 0;
		tmport = wkport + 0x14;
		outb(0x09, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				outb(u3[j++], tmport);
				tmport += 0x06;
			}
			cpu_relax();
		}
		tmport -= 0x08;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x18;
		outb(0x20, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				outb(0, tmport);
				tmport += 0x06;
			}
			cpu_relax();
		}
		tmport -= 0x08;
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x14;
		outb(0x09, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;
		k = 0;
u3p_in1:
		j = inb(tmport);
		if ((j & 0x01) != 0) {
			tmport -= 0x06;
			mbuf[k++] = inb(tmport);
			tmport += 0x06;
			goto u3p_in1;
		}
		if ((j & 0x80) == 0x00) {
			goto u3p_in1;
		}
		tmport -= 0x08;
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x10;
		outb(0x30, tmport);
		tmport = wkport + 0x14;
		outb(0x00, tmport);
		tmport += 0x04;
		outb(0x08, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00);
		tmport -= 0x08;
		j = inb(tmport);
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
		tmport = wkport + 0x1b;
		outb(0x01, tmport);
		tmport = wkport + 0x03;
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[c][i].devsp, tmport++);
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		if ((inb(tmport) != 0x11) && (inb(tmport) != 0x8e)) {
			continue;
		}
		while (inb(tmport) != 0x8e)
			cpu_relax();
try_wide:
		j = 0;
		tmport = wkport + 0x14;
		outb(0x05, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				outb(wide[j++], tmport);
				tmport += 0x06;
			}
			cpu_relax();
		}
		tmport -= 0x08;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x18;
		outb(0x20, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				outb(0, tmport);
				tmport += 0x06;
			}
			cpu_relax();
		}
		tmport -= 0x08;
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x14;
		outb(0xff, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;
		k = 0;
widep_in1:
		j = inb(tmport);
		if ((j & 0x01) != 0) {
			tmport -= 0x06;
			mbuf[k++] = inb(tmport);
			tmport += 0x06;
			goto widep_in1;
		}
		if ((j & 0x80) == 0x00) {
			goto widep_in1;
		}
		tmport -= 0x08;
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x10;
		outb(0x30, tmport);
		tmport = wkport + 0x14;
		outb(0x00, tmport);
		tmport += 0x04;
		outb(0x08, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		j = inb(tmport);
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
		if ((dev->id[c][i].devtype == 0x00) || (dev->id[c][i].devtype == 0x07) ||
		    ((dev->id[c][i].devtype == 0x05) && ((n & 0x10) != 0))) {
			m = 1;
			m = m << i;
			if ((dev->async[c] & m) != 0) {
			   goto set_sync;
			}
		}
		continue;
set_sync:
		if (dev->sp[c][i] == 0x02) {
		   synu[4]=0x0c;
		   synuw[4]=0x0c;
		} else {
		   if (dev->sp[c][i] >= 0x03) {
		      synu[4]=0x0a;
		      synuw[4]=0x0a;
		   }
		}
		tmport = wkport + 0x1b;
		j = 0;
		if ((m & dev->wide_id[c]) != 0) {
			j |= 0x01;
		}
		outb(j, tmport);
		tmport = wkport + 0x03;
		outb(satn[0], tmport++);
		outb(satn[1], tmport++);
		outb(satn[2], tmport++);
		outb(satn[3], tmport++);
		outb(satn[4], tmport++);
		outb(satn[5], tmport++);
		tmport += 0x06;
		outb(0, tmport);
		tmport += 0x02;
		outb(dev->id[c][i].devsp, tmport++);
		outb(0, tmport++);
		outb(satn[6], tmport++);
		outb(satn[7], tmport++);
		tmport += 0x03;
		outb(satn[8], tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		if ((inb(tmport) != 0x11) && (inb(tmport) != 0x8e)) {
			continue;
		}
		while (inb(tmport) != 0x8e)
			cpu_relax();
try_sync:
		j = 0;
		tmport = wkport + 0x14;
		outb(0x06, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;

		while ((inb(tmport) & 0x80) == 0) {
			if ((inb(tmport) & 0x01) != 0) {
				tmport -= 0x06;
				if ((m & dev->wide_id[c]) != 0) {
					if ((m & dev->ultra_map[c]) != 0) {
						outb(synuw[j++], tmport);
					} else {
						outb(synw[j++], tmport);
					}
				} else {
					if ((m & dev->ultra_map[c]) != 0) {
						outb(synu[j++], tmport);
					} else {
						outb(synn[j++], tmport);
					}
				}
				tmport += 0x06;
			}
		}
		tmport -= 0x08;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		j = inb(tmport) & 0x0f;
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
		tmport = wkport + 0x18;
		outb(0x20, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00) {
			if ((inb(tmport) & 0x01) != 0x00) {
				tmport -= 0x06;
				outb(0x00, tmport);
				tmport += 0x06;
			}
			cpu_relax();
		}
		tmport -= 0x08;
		j = inb(tmport);
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
		tmport = wkport + 0x14;
		outb(0x06, tmport);
		tmport += 0x04;
		outb(0x20, tmport);
		tmport += 0x07;
		k = 0;
phase_ins1:
		j = inb(tmport);
		if ((j & 0x01) != 0x00) {
			tmport -= 0x06;
			mbuf[k++] = inb(tmport);
			tmport += 0x06;
			goto phase_ins1;
		}
		if ((j & 0x80) == 0x00) {
			goto phase_ins1;
		}
		tmport -= 0x08;
		while ((inb(tmport) & 0x80) == 0x00);
		j = inb(tmport);
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
		tmport = wkport + 0x10;
		outb(0x30, tmport);
tar_dcons:
		tmport = wkport + 0x14;
		outb(0x00, tmport);
		tmport += 0x04;
		outb(0x08, tmport);
		tmport += 0x07;
		while ((inb(tmport) & 0x80) == 0x00)
			cpu_relax();
		tmport -= 0x08;
		j = inb(tmport);
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
		if (mbuf[4] > 0x0e) {
			mbuf[4] = 0x0e;
		}
		dev->id[c][i].devsp = mbuf[4];
		if (mbuf[3] < 0x0c){
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
	tmport = wkport + 0x16;
	outb(0x80, tmport);
}

module_init(atp870u_init);
module_exit(atp870u_exit);

