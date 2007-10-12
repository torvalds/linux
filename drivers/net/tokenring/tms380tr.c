/*
 *  tms380tr.c: A network driver library for Texas Instruments TMS380-based
 *              Token Ring Adapters.
 *
 *  Originally sktr.c: Written 1997 by Christoph Goos
 *
 *  A fine result of the Linux Systems Network Architecture Project.
 *  http://www.linux-sna.org
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License, incorporated herein by reference.
 *
 *  The following modules are currently available for card support:
 *	- tmspci (Generic PCI card support)
 *	- abyss (Madge PCI support)
 *      - tmsisa (SysKonnect TR4/16 ISA)
 *
 *  Sources:
 *  	- The hardware related parts of this driver are take from
 *  	  the SysKonnect Token Ring driver for Windows NT.
 *  	- I used the IBM Token Ring driver 'ibmtr.c' as a base for this
 *  	  driver, as well as the 'skeleton.c' driver by Donald Becker.
 *  	- Also various other drivers in the linux source tree were taken
 *  	  as samples for some tasks.
 *      - TI TMS380 Second-Generation Token Ring User's Guide
 *  	- TI datasheets for respective chips
 *  	- David Hein at Texas Instruments 
 *  	- Various Madge employees
 *
 *  Maintainer(s):
 *    JS	Jay Schulist		jschlst@samba.org
 *    CG	Christoph Goos		cgoos@syskonnect.de
 *    AF	Adam Fritzler		mid@auk.cx
 *    MLP       Mike Phillips           phillim@amtrak.com
 *    JF	Jochen Friedrich	jochen@scram.de
 *     
 *  Modification History:
 *	29-Aug-97	CG	Created
 *	04-Apr-98	CG	Fixed problems caused by tok_timer_check
 *	10-Apr-98	CG	Fixed lockups at cable disconnection
 *	27-May-98	JS	Formated to Linux Kernel Format
 *	31-May-98	JS	Hacked in PCI support
 *	16-Jun-98	JS	Modulized for multiple cards with one driver
 *	   Sep-99	AF	Renamed to tms380tr (supports more than SK's)
 *      23-Sep-99	AF      Added Compaq and Thomas-Conrad PCI support
 *				Fixed a bug causing double copies on PCI
 *				Fixed for new multicast stuff (2.2/2.3)
 *	25-Sep-99	AF	Uped TPL_NUM from 3 to 9
 *				Removed extraneous 'No free TPL'
 *	22-Dec-99	AF	Added Madge PCI Mk2 support and generalized
 *				parts of the initilization procedure.
 *	30-Dec-99	AF	Turned tms380tr into a library ala 8390.
 *				Madge support is provided in the abyss module
 *				Generic PCI support is in the tmspci module.
 *	30-Nov-00	JF	Updated PCI code to support IO MMU via
 *				pci_map_static(). Alpha uses this MMU for ISA
 *				as well.
 *      14-Jan-01	JF	Fix DMA on ifdown/ifup sequences. Some 
 *      			cleanup.
 *	13-Jan-02	JF	Add spinlock to fix race condition.
 *	09-Nov-02	JF	Fixed printks to not SPAM the console during
 *				normal operation.
 *	30-Dec-02	JF	Removed incorrect __init from 
 *				tms380tr_init_card.
 *	22-Jul-05	JF	Converted to dma-mapping.
 *      			
 *  To do:
 *    1. Multi/Broadcast packet handling (this may have fixed itself)
 *    2. Write a sktrisa module that includes the old ISA support (done)
 *    3. Allow modules to load their own microcode
 *    4. Speed up the BUD process -- freezing the kernel for 3+sec is
 *         quite unacceptable.
 *    5. Still a few remaining stalls when the cable is unplugged.
 */

#ifdef MODULE
static const char version[] = "tms380tr.c: v1.10 30/12/2002 by Christoph Goos, Adam Fritzler\n";
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/trdevice.h>
#include <linux/firmware.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "tms380tr.h"		/* Our Stuff */

/* Use 0 for production, 1 for verification, 2 for debug, and
 * 3 for very verbose debug.
 */
#ifndef TMS380TR_DEBUG
#define TMS380TR_DEBUG 0
#endif
static unsigned int tms380tr_debug = TMS380TR_DEBUG;

/* Index to functions, as function prototypes.
 * Alphabetical by function name.
 */

/* "A" */
/* "B" */
static int      tms380tr_bringup_diags(struct net_device *dev);
/* "C" */
static void	tms380tr_cancel_tx_queue(struct net_local* tp);
static int 	tms380tr_chipset_init(struct net_device *dev);
static void 	tms380tr_chk_irq(struct net_device *dev);
static void 	tms380tr_chk_outstanding_cmds(struct net_device *dev);
static void 	tms380tr_chk_src_addr(unsigned char *frame, unsigned char *hw_addr);
static unsigned char tms380tr_chk_ssb(struct net_local *tp, unsigned short IrqType);
int	 	tms380tr_close(struct net_device *dev);
static void 	tms380tr_cmd_status_irq(struct net_device *dev);
/* "D" */
static void 	tms380tr_disable_interrupts(struct net_device *dev);
#if TMS380TR_DEBUG > 0
static void 	tms380tr_dump(unsigned char *Data, int length);
#endif
/* "E" */
static void 	tms380tr_enable_interrupts(struct net_device *dev);
static void 	tms380tr_exec_cmd(struct net_device *dev, unsigned short Command);
static void 	tms380tr_exec_sifcmd(struct net_device *dev, unsigned int WriteValue);
/* "F" */
/* "G" */
static struct net_device_stats *tms380tr_get_stats(struct net_device *dev);
/* "H" */
static int 	tms380tr_hardware_send_packet(struct sk_buff *skb,
			struct net_device *dev);
/* "I" */
static int 	tms380tr_init_adapter(struct net_device *dev);
static void 	tms380tr_init_ipb(struct net_local *tp);
static void 	tms380tr_init_net_local(struct net_device *dev);
static void 	tms380tr_init_opb(struct net_device *dev);
/* "M" */
/* "O" */
int		tms380tr_open(struct net_device *dev);
static void	tms380tr_open_adapter(struct net_device *dev);
/* "P" */
/* "R" */
static void 	tms380tr_rcv_status_irq(struct net_device *dev);
static int 	tms380tr_read_ptr(struct net_device *dev);
static void 	tms380tr_read_ram(struct net_device *dev, unsigned char *Data,
			unsigned short Address, int Length);
static int 	tms380tr_reset_adapter(struct net_device *dev);
static void 	tms380tr_reset_interrupt(struct net_device *dev);
static void 	tms380tr_ring_status_irq(struct net_device *dev);
/* "S" */
static int 	tms380tr_send_packet(struct sk_buff *skb, struct net_device *dev);
static void 	tms380tr_set_multicast_list(struct net_device *dev);
static int	tms380tr_set_mac_address(struct net_device *dev, void *addr);
/* "T" */
static void 	tms380tr_timer_chk(unsigned long data);
static void 	tms380tr_timer_end_wait(unsigned long data);
static void 	tms380tr_tx_status_irq(struct net_device *dev);
/* "U" */
static void 	tms380tr_update_rcv_stats(struct net_local *tp,
			unsigned char DataPtr[], unsigned int Length);
/* "W" */
void	 	tms380tr_wait(unsigned long time);
static void 	tms380tr_write_rpl_status(RPL *rpl, unsigned int Status);
static void 	tms380tr_write_tpl_status(TPL *tpl, unsigned int Status);

#define SIFREADB(reg) (((struct net_local *)dev->priv)->sifreadb(dev, reg))
#define SIFWRITEB(val, reg) (((struct net_local *)dev->priv)->sifwriteb(dev, val, reg))
#define SIFREADW(reg) (((struct net_local *)dev->priv)->sifreadw(dev, reg))
#define SIFWRITEW(val, reg) (((struct net_local *)dev->priv)->sifwritew(dev, val, reg))



#if 0 /* TMS380TR_DEBUG > 0 */
static int madgemc_sifprobe(struct net_device *dev)
{
        unsigned char old, chk1, chk2;
	
	old = SIFREADB(SIFADR);  /* Get the old SIFADR value */

        chk1 = 0;       /* Begin with check value 0 */
        do {
		madgemc_setregpage(dev, 0);
                /* Write new SIFADR value */
		SIFWRITEB(chk1, SIFADR);
		chk2 = SIFREADB(SIFADR);
		if (chk2 != chk1)
			return -1;
		
		madgemc_setregpage(dev, 1);
                /* Read, invert and write */
		chk2 = SIFREADB(SIFADD);
		if (chk2 != chk1)
			return -1;

		madgemc_setregpage(dev, 0);
                chk2 ^= 0x0FE;
		SIFWRITEB(chk2, SIFADR);

                /* Read, invert and compare */
		madgemc_setregpage(dev, 1);
		chk2 = SIFREADB(SIFADD);
		madgemc_setregpage(dev, 0);
                chk2 ^= 0x0FE;

                if(chk1 != chk2)
                        return (-1);    /* No adapter */
                chk1 -= 2;
        } while(chk1 != 0);     /* Repeat 128 times (all byte values) */

	madgemc_setregpage(dev, 0); /* sanity */
        /* Restore the SIFADR value */
	SIFWRITEB(old, SIFADR);

        return (0);
}
#endif

/*
 * Open/initialize the board. This is called sometime after
 * booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
int tms380tr_open(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	int err;
	
	/* init the spinlock */
	spin_lock_init(&tp->lock);
	init_timer(&tp->timer);

	/* Reset the hardware here. Don't forget to set the station address. */

#ifdef CONFIG_ISA
	if(dev->dma > 0) 
	{
		unsigned long flags=claim_dma_lock();
		disable_dma(dev->dma);
		set_dma_mode(dev->dma, DMA_MODE_CASCADE);
		enable_dma(dev->dma);
		release_dma_lock(flags);
	}
#endif
	
	err = tms380tr_chipset_init(dev);
  	if(err)
	{
		printk(KERN_INFO "%s: Chipset initialization error\n", 
			dev->name);
		return (-1);
	}

	tp->timer.expires	= jiffies + 30*HZ;
	tp->timer.function	= tms380tr_timer_end_wait;
	tp->timer.data		= (unsigned long)dev;
	add_timer(&tp->timer);

	printk(KERN_DEBUG "%s: Adapter RAM size: %dK\n", 
	       dev->name, tms380tr_read_ptr(dev));

	tms380tr_enable_interrupts(dev);
	tms380tr_open_adapter(dev);

	netif_start_queue(dev);
	
	/* Wait for interrupt from hardware. If interrupt does not come,
	 * there will be a timeout from the timer.
	 */
	tp->Sleeping = 1;
	interruptible_sleep_on(&tp->wait_for_tok_int);
	del_timer(&tp->timer);

	/* If AdapterVirtOpenFlag is 1, the adapter is now open for use */
	if(tp->AdapterVirtOpenFlag == 0)
	{
		tms380tr_disable_interrupts(dev);
		return (-1);
	}

	tp->StartTime = jiffies;

	/* Start function control timer */
	tp->timer.expires	= jiffies + 2*HZ;
	tp->timer.function	= tms380tr_timer_chk;
	tp->timer.data		= (unsigned long)dev;
	add_timer(&tp->timer);

	return (0);
}

/*
 * Timeout function while waiting for event
 */
static void tms380tr_timer_end_wait(unsigned long data)
{
	struct net_device *dev = (struct net_device*)data;
	struct net_local *tp = netdev_priv(dev);

	if(tp->Sleeping)
	{
		tp->Sleeping = 0;
		wake_up_interruptible(&tp->wait_for_tok_int);
	}

	return;
}

/*
 * Initialize the chipset
 */
static int tms380tr_chipset_init(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	int err;

	tms380tr_init_ipb(tp);
	tms380tr_init_opb(dev);
	tms380tr_init_net_local(dev);

	if(tms380tr_debug > 3)
		printk(KERN_DEBUG "%s: Resetting adapter...\n", dev->name);
	err = tms380tr_reset_adapter(dev);
	if(err < 0)
		return (-1);

	if(tms380tr_debug > 3)
		printk(KERN_DEBUG "%s: Bringup diags...\n", dev->name);
	err = tms380tr_bringup_diags(dev);
	if(err < 0)
		return (-1);

	if(tms380tr_debug > 3)
		printk(KERN_DEBUG "%s: Init adapter...\n", dev->name);
	err = tms380tr_init_adapter(dev);
	if(err < 0)
		return (-1);

	if(tms380tr_debug > 3)
		printk(KERN_DEBUG "%s: Done!\n", dev->name);
	return (0);
}

/*
 * Initializes the net_local structure.
 */
static void tms380tr_init_net_local(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	int i;
	dma_addr_t dmabuf;

	tp->scb.CMD	= 0;
	tp->scb.Parm[0] = 0;
	tp->scb.Parm[1] = 0;

	tp->ssb.STS	= 0;
	tp->ssb.Parm[0] = 0;
	tp->ssb.Parm[1] = 0;
	tp->ssb.Parm[2] = 0;

	tp->CMDqueue	= 0;

	tp->AdapterOpenFlag	= 0;
	tp->AdapterVirtOpenFlag = 0;
	tp->ScbInUse		= 0;
	tp->OpenCommandIssued	= 0;
	tp->ReOpenInProgress	= 0;
	tp->HaltInProgress	= 0;
	tp->TransmitHaltScheduled = 0;
	tp->LobeWireFaultLogged	= 0;
	tp->LastOpenStatus	= 0;
	tp->MaxPacketSize	= DEFAULT_PACKET_SIZE;

	/* Create circular chain of transmit lists */
	for (i = 0; i < TPL_NUM; i++)
	{
		tp->Tpl[i].NextTPLAddr = htonl(((char *)(&tp->Tpl[(i+1) % TPL_NUM]) - (char *)tp) + tp->dmabuffer); /* DMA buffer may be MMU driven */
		tp->Tpl[i].Status	= 0;
		tp->Tpl[i].FrameSize	= 0;
		tp->Tpl[i].FragList[0].DataCount	= 0;
		tp->Tpl[i].FragList[0].DataAddr		= 0;
		tp->Tpl[i].NextTPLPtr	= &tp->Tpl[(i+1) % TPL_NUM];
		tp->Tpl[i].MData	= NULL;
		tp->Tpl[i].TPLIndex	= i;
		tp->Tpl[i].DMABuff	= 0;
		tp->Tpl[i].BusyFlag	= 0;
	}

	tp->TplFree = tp->TplBusy = &tp->Tpl[0];

	/* Create circular chain of receive lists */
	for (i = 0; i < RPL_NUM; i++)
	{
		tp->Rpl[i].NextRPLAddr = htonl(((char *)(&tp->Rpl[(i+1) % RPL_NUM]) - (char *)tp) + tp->dmabuffer); /* DMA buffer may be MMU driven */
		tp->Rpl[i].Status = (RX_VALID | RX_START_FRAME | RX_END_FRAME | RX_FRAME_IRQ);
		tp->Rpl[i].FrameSize = 0;
		tp->Rpl[i].FragList[0].DataCount = cpu_to_be16((unsigned short)tp->MaxPacketSize);

		/* Alloc skb and point adapter to data area */
		tp->Rpl[i].Skb = dev_alloc_skb(tp->MaxPacketSize);
			tp->Rpl[i].DMABuff = 0;

		/* skb == NULL ? then use local buffer */
		if(tp->Rpl[i].Skb == NULL)
		{
			tp->Rpl[i].SkbStat = SKB_UNAVAILABLE;
			tp->Rpl[i].FragList[0].DataAddr = htonl(((char *)tp->LocalRxBuffers[i] - (char *)tp) + tp->dmabuffer);
			tp->Rpl[i].MData = tp->LocalRxBuffers[i];
		}
		else	/* SKB != NULL */
		{
			tp->Rpl[i].Skb->dev = dev;
			skb_put(tp->Rpl[i].Skb, tp->MaxPacketSize);

			/* data unreachable for DMA ? then use local buffer */
			dmabuf = dma_map_single(tp->pdev, tp->Rpl[i].Skb->data, tp->MaxPacketSize, DMA_FROM_DEVICE);
			if(tp->dmalimit && (dmabuf + tp->MaxPacketSize > tp->dmalimit))
			{
				tp->Rpl[i].SkbStat = SKB_DATA_COPY;
				tp->Rpl[i].FragList[0].DataAddr = htonl(((char *)tp->LocalRxBuffers[i] - (char *)tp) + tp->dmabuffer);
				tp->Rpl[i].MData = tp->LocalRxBuffers[i];
			}
			else	/* DMA directly in skb->data */
			{
				tp->Rpl[i].SkbStat = SKB_DMA_DIRECT;
				tp->Rpl[i].FragList[0].DataAddr = htonl(dmabuf);
				tp->Rpl[i].MData = tp->Rpl[i].Skb->data;
				tp->Rpl[i].DMABuff = dmabuf;
			}
		}

		tp->Rpl[i].NextRPLPtr = &tp->Rpl[(i+1) % RPL_NUM];
		tp->Rpl[i].RPLIndex = i;
	}

	tp->RplHead = &tp->Rpl[0];
	tp->RplTail = &tp->Rpl[RPL_NUM-1];
	tp->RplTail->Status = (RX_START_FRAME | RX_END_FRAME | RX_FRAME_IRQ);

	return;
}

/*
 * Initializes the initialisation parameter block.
 */
static void tms380tr_init_ipb(struct net_local *tp)
{
	tp->ipb.Init_Options	= BURST_MODE;
	tp->ipb.CMD_Status_IV	= 0;
	tp->ipb.TX_IV		= 0;
	tp->ipb.RX_IV		= 0;
	tp->ipb.Ring_Status_IV	= 0;
	tp->ipb.SCB_Clear_IV	= 0;
	tp->ipb.Adapter_CHK_IV	= 0;
	tp->ipb.RX_Burst_Size	= BURST_SIZE;
	tp->ipb.TX_Burst_Size	= BURST_SIZE;
	tp->ipb.DMA_Abort_Thrhld = DMA_RETRIES;
	tp->ipb.SCB_Addr	= 0;
	tp->ipb.SSB_Addr	= 0;

	return;
}

/*
 * Initializes the open parameter block.
 */
static void tms380tr_init_opb(struct net_device *dev)
{
	struct net_local *tp;
	unsigned long Addr;
	unsigned short RplSize    = RPL_SIZE;
	unsigned short TplSize    = TPL_SIZE;
	unsigned short BufferSize = BUFFER_SIZE;
	int i;

	tp = netdev_priv(dev);

	tp->ocpl.OPENOptions 	 = 0;
	tp->ocpl.OPENOptions 	|= ENABLE_FULL_DUPLEX_SELECTION;
	tp->ocpl.FullDuplex 	 = 0;
	tp->ocpl.FullDuplex 	|= OPEN_FULL_DUPLEX_OFF;

        /* 
	 * Set node address 
	 *
	 * We go ahead and put it in the OPB even though on
	 * most of the generic adapters this isn't required.
	 * Its simpler this way.  -- ASF
	 */
        for (i=0;i<6;i++)
                tp->ocpl.NodeAddr[i] = ((unsigned char *)dev->dev_addr)[i];

	tp->ocpl.GroupAddr	 = 0;
	tp->ocpl.FunctAddr	 = 0;
	tp->ocpl.RxListSize	 = cpu_to_be16((unsigned short)RplSize);
	tp->ocpl.TxListSize	 = cpu_to_be16((unsigned short)TplSize);
	tp->ocpl.BufSize	 = cpu_to_be16((unsigned short)BufferSize);
	tp->ocpl.Reserved	 = 0;
	tp->ocpl.TXBufMin	 = TX_BUF_MIN;
	tp->ocpl.TXBufMax	 = TX_BUF_MAX;

	Addr = htonl(((char *)tp->ProductID - (char *)tp) + tp->dmabuffer);

	tp->ocpl.ProdIDAddr[0]	 = LOWORD(Addr);
	tp->ocpl.ProdIDAddr[1]	 = HIWORD(Addr);

	return;
}

/*
 * Send OPEN command to adapter
 */
static void tms380tr_open_adapter(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);

	if(tp->OpenCommandIssued)
		return;

	tp->OpenCommandIssued = 1;
	tms380tr_exec_cmd(dev, OC_OPEN);

	return;
}

/*
 * Clear the adapter's interrupt flag. Clear system interrupt enable
 * (SINTEN): disable adapter to system interrupts.
 */
static void tms380tr_disable_interrupts(struct net_device *dev)
{
	SIFWRITEB(0, SIFACL);

	return;
}

/*
 * Set the adapter's interrupt flag. Set system interrupt enable
 * (SINTEN): enable adapter to system interrupts.
 */
static void tms380tr_enable_interrupts(struct net_device *dev)
{
	SIFWRITEB(ACL_SINTEN, SIFACL);

	return;
}

/*
 * Put command in command queue, try to execute it.
 */
static void tms380tr_exec_cmd(struct net_device *dev, unsigned short Command)
{
	struct net_local *tp = netdev_priv(dev);

	tp->CMDqueue |= Command;
	tms380tr_chk_outstanding_cmds(dev);

	return;
}

static void tms380tr_timeout(struct net_device *dev)
{
	/*
	 * If we get here, some higher level has decided we are broken.
	 * There should really be a "kick me" function call instead.
	 *
	 * Resetting the token ring adapter takes a long time so just
	 * fake transmission time and go on trying. Our own timeout
	 * routine is in tms380tr_timer_chk()
	 */
	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

/*
 * Gets skb from system, queues it and checks if it can be sent
 */
static int tms380tr_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	int err;

	err = tms380tr_hardware_send_packet(skb, dev);
	if(tp->TplFree->NextTPLPtr->BusyFlag)
		netif_stop_queue(dev);
	return (err);
}

/*
 * Move frames into adapter tx queue
 */
static int tms380tr_hardware_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	TPL *tpl;
	short length;
	unsigned char *buf;
	unsigned long flags;
	int i;
	dma_addr_t dmabuf, newbuf;
	struct net_local *tp = netdev_priv(dev);
   
	/* Try to get a free TPL from the chain.
	 *
	 * NOTE: We *must* always leave one unused TPL in the chain,
	 * because otherwise the adapter might send frames twice.
	 */
	spin_lock_irqsave(&tp->lock, flags);
	if(tp->TplFree->NextTPLPtr->BusyFlag)  { /* No free TPL */
		if (tms380tr_debug > 0)
			printk(KERN_DEBUG "%s: No free TPL\n", dev->name);
		spin_unlock_irqrestore(&tp->lock, flags);
		return 1;
	}

	dmabuf = 0;

	/* Is buffer reachable for Busmaster-DMA? */

	length	= skb->len;
	dmabuf = dma_map_single(tp->pdev, skb->data, length, DMA_TO_DEVICE);
	if(tp->dmalimit && (dmabuf + length > tp->dmalimit)) {
		/* Copy frame to local buffer */
		dma_unmap_single(tp->pdev, dmabuf, length, DMA_TO_DEVICE);
		dmabuf  = 0;
		i 	= tp->TplFree->TPLIndex;
		buf 	= tp->LocalTxBuffers[i];
		skb_copy_from_linear_data(skb, buf, length);
		newbuf 	= ((char *)buf - (char *)tp) + tp->dmabuffer;
	}
	else {
		/* Send direct from skb->data */
		newbuf	= dmabuf;
		buf	= skb->data;
	}
	/* Source address in packet? */
	tms380tr_chk_src_addr(buf, dev->dev_addr);
	tp->LastSendTime	= jiffies;
	tpl 			= tp->TplFree;	/* Get the "free" TPL */
	tpl->BusyFlag 		= 1;		/* Mark TPL as busy */
	tp->TplFree 		= tpl->NextTPLPtr;
    
	/* Save the skb for delayed return of skb to system */
	tpl->Skb = skb;
	tpl->DMABuff = dmabuf;
	tpl->FragList[0].DataCount = cpu_to_be16((unsigned short)length);
	tpl->FragList[0].DataAddr  = htonl(newbuf);

	/* Write the data length in the transmit list. */
	tpl->FrameSize 	= cpu_to_be16((unsigned short)length);
	tpl->MData 	= buf;

	/* Transmit the frame and set the status values. */
	tms380tr_write_tpl_status(tpl, TX_VALID | TX_START_FRAME
				| TX_END_FRAME | TX_PASS_SRC_ADDR
				| TX_FRAME_IRQ);

	/* Let adapter send the frame. */
	tms380tr_exec_sifcmd(dev, CMD_TX_VALID);
	spin_unlock_irqrestore(&tp->lock, flags);

	return 0;
}

/*
 * Write the given value to the 'Status' field of the specified TPL.
 * NOTE: This function should be used whenever the status of any TPL must be
 * modified by the driver, because the compiler may otherwise change the
 * order of instructions such that writing the TPL status may be executed at
 * an undesireable time. When this function is used, the status is always
 * written when the function is called.
 */
static void tms380tr_write_tpl_status(TPL *tpl, unsigned int Status)
{
	tpl->Status = Status;
}

static void tms380tr_chk_src_addr(unsigned char *frame, unsigned char *hw_addr)
{
	unsigned char SRBit;

	if((((unsigned long)frame[8]) & ~0x80) != 0)	/* Compare 4 bytes */
		return;
	if((unsigned short)frame[12] != 0)		/* Compare 2 bytes */
		return;

	SRBit = frame[8] & 0x80;
	memcpy(&frame[8], hw_addr, 6);
	frame[8] |= SRBit;

	return;
}

/*
 * The timer routine: Check if adapter still open and working, reopen if not. 
 */
static void tms380tr_timer_chk(unsigned long data)
{
	struct net_device *dev = (struct net_device*)data;
	struct net_local *tp = netdev_priv(dev);

	if(tp->HaltInProgress)
		return;

	tms380tr_chk_outstanding_cmds(dev);
	if(time_before(tp->LastSendTime + SEND_TIMEOUT, jiffies)
		&& (tp->TplFree != tp->TplBusy))
	{
		/* Anything to send, but stalled too long */
		tp->LastSendTime = jiffies;
		tms380tr_exec_cmd(dev, OC_CLOSE);	/* Does reopen automatically */
	}

	tp->timer.expires = jiffies + 2*HZ;
	add_timer(&tp->timer);

	if(tp->AdapterOpenFlag || tp->ReOpenInProgress)
		return;
	tp->ReOpenInProgress = 1;
	tms380tr_open_adapter(dev);

	return;
}

/*
 * The typical workload of the driver: Handle the network interface interrupts.
 */
irqreturn_t tms380tr_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *tp;
	unsigned short irq_type;
	int handled = 0;

	tp = netdev_priv(dev);

	irq_type = SIFREADW(SIFSTS);

	while(irq_type & STS_SYSTEM_IRQ) {
		handled = 1;
		irq_type &= STS_IRQ_MASK;

		if(!tms380tr_chk_ssb(tp, irq_type)) {
			printk(KERN_DEBUG "%s: DATA LATE occurred\n", dev->name);
			break;
		}

		switch(irq_type) {
		case STS_IRQ_RECEIVE_STATUS:
			tms380tr_reset_interrupt(dev);
			tms380tr_rcv_status_irq(dev);
			break;

		case STS_IRQ_TRANSMIT_STATUS:
			/* Check if TRANSMIT.HALT command is complete */
			if(tp->ssb.Parm[0] & COMMAND_COMPLETE) {
				tp->TransmitCommandActive = 0;
					tp->TransmitHaltScheduled = 0;

					/* Issue a new transmit command. */
					tms380tr_exec_cmd(dev, OC_TRANSMIT);
				}

				tms380tr_reset_interrupt(dev);
				tms380tr_tx_status_irq(dev);
				break;

		case STS_IRQ_COMMAND_STATUS:
			/* The SSB contains status of last command
			 * other than receive/transmit.
			 */
			tms380tr_cmd_status_irq(dev);
			break;
			
		case STS_IRQ_SCB_CLEAR:
			/* The SCB is free for another command. */
			tp->ScbInUse = 0;
			tms380tr_chk_outstanding_cmds(dev);
			break;
			
		case STS_IRQ_RING_STATUS:
			tms380tr_ring_status_irq(dev);
			break;

		case STS_IRQ_ADAPTER_CHECK:
			tms380tr_chk_irq(dev);
			break;

		case STS_IRQ_LLC_STATUS:
			printk(KERN_DEBUG "tms380tr: unexpected LLC status IRQ\n");
			break;
			
		case STS_IRQ_TIMER:
			printk(KERN_DEBUG "tms380tr: unexpected Timer IRQ\n");
			break;
			
		case STS_IRQ_RECEIVE_PENDING:
			printk(KERN_DEBUG "tms380tr: unexpected Receive Pending IRQ\n");
			break;
			
		default:
			printk(KERN_DEBUG "Unknown Token Ring IRQ (0x%04x)\n", irq_type);
			break;
		}

		/* Reset system interrupt if not already done. */
		if(irq_type != STS_IRQ_TRANSMIT_STATUS
			&& irq_type != STS_IRQ_RECEIVE_STATUS) {
			tms380tr_reset_interrupt(dev);
		}

		irq_type = SIFREADW(SIFSTS);
	}

	return IRQ_RETVAL(handled);
}

/*
 *  Reset the INTERRUPT SYSTEM bit and issue SSB CLEAR command.
 */
static void tms380tr_reset_interrupt(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	SSB *ssb = &tp->ssb;

	/*
	 * [Workaround for "Data Late"]
	 * Set all fields of the SSB to well-defined values so we can
	 * check if the adapter has written the SSB.
	 */

	ssb->STS	= (unsigned short) -1;
	ssb->Parm[0] 	= (unsigned short) -1;
	ssb->Parm[1] 	= (unsigned short) -1;
	ssb->Parm[2] 	= (unsigned short) -1;

	/* Free SSB by issuing SSB_CLEAR command after reading IRQ code
	 * and clear STS_SYSTEM_IRQ bit: enable adapter for further interrupts.
	 */
	tms380tr_exec_sifcmd(dev, CMD_SSB_CLEAR | CMD_CLEAR_SYSTEM_IRQ);

	return;
}

/*
 * Check if the SSB has actually been written by the adapter.
 */
static unsigned char tms380tr_chk_ssb(struct net_local *tp, unsigned short IrqType)
{
	SSB *ssb = &tp->ssb;	/* The address of the SSB. */

	/* C 0 1 2 INTERRUPT CODE
	 * - - - - --------------
	 * 1 1 1 1 TRANSMIT STATUS
	 * 1 1 1 1 RECEIVE STATUS
	 * 1 ? ? 0 COMMAND STATUS
	 * 0 0 0 0 SCB CLEAR
	 * 1 1 0 0 RING STATUS
	 * 0 0 0 0 ADAPTER CHECK
	 *
	 * 0 = SSB field not affected by interrupt
	 * 1 = SSB field is affected by interrupt
	 *
	 * C = SSB ADDRESS +0: COMMAND
	 * 0 = SSB ADDRESS +2: STATUS 0
	 * 1 = SSB ADDRESS +4: STATUS 1
	 * 2 = SSB ADDRESS +6: STATUS 2
	 */

	/* Check if this interrupt does use the SSB. */

	if(IrqType != STS_IRQ_TRANSMIT_STATUS
		&& IrqType != STS_IRQ_RECEIVE_STATUS
		&& IrqType != STS_IRQ_COMMAND_STATUS
		&& IrqType != STS_IRQ_RING_STATUS)
	{
		return (1);	/* SSB not involved. */
	}

	/* Note: All fields of the SSB have been set to all ones (-1) after it
	 * has last been used by the software (see DriverIsr()).
	 *
	 * Check if the affected SSB fields are still unchanged.
	 */

	if(ssb->STS == (unsigned short) -1)
		return (0);	/* Command field not yet available. */
	if(IrqType == STS_IRQ_COMMAND_STATUS)
		return (1);	/* Status fields not always affected. */
	if(ssb->Parm[0] == (unsigned short) -1)
		return (0);	/* Status 1 field not yet available. */
	if(IrqType == STS_IRQ_RING_STATUS)
		return (1);	/* Status 2 & 3 fields not affected. */

	/* Note: At this point, the interrupt is either TRANSMIT or RECEIVE. */
	if(ssb->Parm[1] == (unsigned short) -1)
		return (0);	/* Status 2 field not yet available. */
	if(ssb->Parm[2] == (unsigned short) -1)
		return (0);	/* Status 3 field not yet available. */

	return (1);	/* All SSB fields have been written by the adapter. */
}

/*
 * Evaluates the command results status in the SSB status field.
 */
static void tms380tr_cmd_status_irq(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	unsigned short ssb_cmd, ssb_parm_0;
	unsigned short ssb_parm_1;
	char *open_err = "Open error -";
	char *code_err = "Open code -";

	/* Copy the ssb values to local variables */
	ssb_cmd    = tp->ssb.STS;
	ssb_parm_0 = tp->ssb.Parm[0];
	ssb_parm_1 = tp->ssb.Parm[1];

	if(ssb_cmd == OPEN)
	{
		tp->Sleeping = 0;
		if(!tp->ReOpenInProgress)
	    		wake_up_interruptible(&tp->wait_for_tok_int);

		tp->OpenCommandIssued = 0;
		tp->ScbInUse = 0;

		if((ssb_parm_0 & 0x00FF) == GOOD_COMPLETION)
		{
			/* Success, the adapter is open. */
			tp->LobeWireFaultLogged	= 0;
			tp->AdapterOpenFlag 	= 1;
			tp->AdapterVirtOpenFlag = 1;
			tp->TransmitCommandActive = 0;
			tms380tr_exec_cmd(dev, OC_TRANSMIT);
			tms380tr_exec_cmd(dev, OC_RECEIVE);

			if(tp->ReOpenInProgress)
				tp->ReOpenInProgress = 0;

			return;
		}
		else 	/* The adapter did not open. */
		{
	    		if(ssb_parm_0 & NODE_ADDR_ERROR)
				printk(KERN_INFO "%s: Node address error\n",
					dev->name);
	    		if(ssb_parm_0 & LIST_SIZE_ERROR)
				printk(KERN_INFO "%s: List size error\n",
					dev->name);
	    		if(ssb_parm_0 & BUF_SIZE_ERROR)
				printk(KERN_INFO "%s: Buffer size error\n",
					dev->name);
	    		if(ssb_parm_0 & TX_BUF_COUNT_ERROR)
				printk(KERN_INFO "%s: Tx buffer count error\n",
					dev->name);
	    		if(ssb_parm_0 & INVALID_OPEN_OPTION)
				printk(KERN_INFO "%s: Invalid open option\n",
					dev->name);
	    		if(ssb_parm_0 & OPEN_ERROR)
			{
				/* Show the open phase. */
				switch(ssb_parm_0 & OPEN_PHASES_MASK)
				{
					case LOBE_MEDIA_TEST:
						if(!tp->LobeWireFaultLogged)
						{
							tp->LobeWireFaultLogged = 1;
							printk(KERN_INFO "%s: %s Lobe wire fault (check cable !).\n", dev->name, open_err);
		    				}
						tp->ReOpenInProgress	= 1;
						tp->AdapterOpenFlag 	= 0;
						tp->AdapterVirtOpenFlag = 1;
						tms380tr_open_adapter(dev);
						return;

					case PHYSICAL_INSERTION:
						printk(KERN_INFO "%s: %s Physical insertion.\n", dev->name, open_err);
						break;

					case ADDRESS_VERIFICATION:
						printk(KERN_INFO "%s: %s Address verification.\n", dev->name, open_err);
						break;

					case PARTICIPATION_IN_RING_POLL:
						printk(KERN_INFO "%s: %s Participation in ring poll.\n", dev->name, open_err);
						break;

					case REQUEST_INITIALISATION:
						printk(KERN_INFO "%s: %s Request initialisation.\n", dev->name, open_err);
						break;

					case FULLDUPLEX_CHECK:
						printk(KERN_INFO "%s: %s Full duplex check.\n", dev->name, open_err);
						break;

					default:
						printk(KERN_INFO "%s: %s Unknown open phase\n", dev->name, open_err);
						break;
				}

				/* Show the open errors. */
				switch(ssb_parm_0 & OPEN_ERROR_CODES_MASK)
				{
					case OPEN_FUNCTION_FAILURE:
						printk(KERN_INFO "%s: %s OPEN_FUNCTION_FAILURE", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_FUNCTION_FAILURE;
						break;

					case OPEN_SIGNAL_LOSS:
						printk(KERN_INFO "%s: %s OPEN_SIGNAL_LOSS\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_SIGNAL_LOSS;
						break;

					case OPEN_TIMEOUT:
						printk(KERN_INFO "%s: %s OPEN_TIMEOUT\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_TIMEOUT;
						break;

					case OPEN_RING_FAILURE:
						printk(KERN_INFO "%s: %s OPEN_RING_FAILURE\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_RING_FAILURE;
						break;

					case OPEN_RING_BEACONING:
						printk(KERN_INFO "%s: %s OPEN_RING_BEACONING\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_RING_BEACONING;
						break;

					case OPEN_DUPLICATE_NODEADDR:
						printk(KERN_INFO "%s: %s OPEN_DUPLICATE_NODEADDR\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_DUPLICATE_NODEADDR;
						break;

					case OPEN_REQUEST_INIT:
						printk(KERN_INFO "%s: %s OPEN_REQUEST_INIT\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_REQUEST_INIT;
						break;

					case OPEN_REMOVE_RECEIVED:
						printk(KERN_INFO "%s: %s OPEN_REMOVE_RECEIVED", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_REMOVE_RECEIVED;
						break;

					case OPEN_FULLDUPLEX_SET:
						printk(KERN_INFO "%s: %s OPEN_FULLDUPLEX_SET\n", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_FULLDUPLEX_SET;
						break;

					default:
						printk(KERN_INFO "%s: %s Unknown open err code", dev->name, code_err);
						tp->LastOpenStatus =
							OPEN_FUNCTION_FAILURE;
						break;
				}
			}

			tp->AdapterOpenFlag 	= 0;
			tp->AdapterVirtOpenFlag = 0;

			return;
		}
	}
	else
	{
		if(ssb_cmd != READ_ERROR_LOG)
			return;

		/* Add values from the error log table to the MAC
		 * statistics counters and update the errorlogtable
		 * memory.
		 */
		tp->MacStat.line_errors += tp->errorlogtable.Line_Error;
		tp->MacStat.burst_errors += tp->errorlogtable.Burst_Error;
		tp->MacStat.A_C_errors += tp->errorlogtable.ARI_FCI_Error;
		tp->MacStat.lost_frames += tp->errorlogtable.Lost_Frame_Error;
		tp->MacStat.recv_congest_count += tp->errorlogtable.Rx_Congest_Error;
		tp->MacStat.rx_errors += tp->errorlogtable.Rx_Congest_Error;
		tp->MacStat.frame_copied_errors += tp->errorlogtable.Frame_Copied_Error;
		tp->MacStat.token_errors += tp->errorlogtable.Token_Error;
		tp->MacStat.dummy1 += tp->errorlogtable.DMA_Bus_Error;
		tp->MacStat.dummy1 += tp->errorlogtable.DMA_Parity_Error;
		tp->MacStat.abort_delimiters += tp->errorlogtable.AbortDelimeters;
		tp->MacStat.frequency_errors += tp->errorlogtable.Frequency_Error;
		tp->MacStat.internal_errors += tp->errorlogtable.Internal_Error;
	}

	return;
}

/*
 * The inverse routine to tms380tr_open().
 */
int tms380tr_close(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	netif_stop_queue(dev);
	
	del_timer(&tp->timer);

	/* Flush the Tx and disable Rx here. */

	tp->HaltInProgress 	= 1;
	tms380tr_exec_cmd(dev, OC_CLOSE);
	tp->timer.expires	= jiffies + 1*HZ;
	tp->timer.function 	= tms380tr_timer_end_wait;
	tp->timer.data 		= (unsigned long)dev;
	add_timer(&tp->timer);

	tms380tr_enable_interrupts(dev);

	tp->Sleeping = 1;
	interruptible_sleep_on(&tp->wait_for_tok_int);
	tp->TransmitCommandActive = 0;
    
	del_timer(&tp->timer);
	tms380tr_disable_interrupts(dev);
   
#ifdef CONFIG_ISA
	if(dev->dma > 0) 
	{
		unsigned long flags=claim_dma_lock();
		disable_dma(dev->dma);
		release_dma_lock(flags);
	}
#endif
	
	SIFWRITEW(0xFF00, SIFCMD);
#if 0
	if(dev->dma > 0) /* what the? */
		SIFWRITEB(0xff, POSREG);
#endif
	tms380tr_cancel_tx_queue(tp);

	return (0);
}

/*
 * Get the current statistics. This may be called with the card open
 * or closed.
 */
static struct net_device_stats *tms380tr_get_stats(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);

	return ((struct net_device_stats *)&tp->MacStat);
}

/*
 * Set or clear the multicast filter for this adapter.
 */
static void tms380tr_set_multicast_list(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	unsigned int OpenOptions;
	
	OpenOptions = tp->ocpl.OPENOptions &
		~(PASS_ADAPTER_MAC_FRAMES
		  | PASS_ATTENTION_FRAMES
		  | PASS_BEACON_MAC_FRAMES
		  | COPY_ALL_MAC_FRAMES
		  | COPY_ALL_NON_MAC_FRAMES);
	
	tp->ocpl.FunctAddr = 0;
	
	if(dev->flags & IFF_PROMISC)
		/* Enable promiscuous mode */
		OpenOptions |= COPY_ALL_NON_MAC_FRAMES |
			COPY_ALL_MAC_FRAMES;
	else
	{
		if(dev->flags & IFF_ALLMULTI)
		{
			/* Disable promiscuous mode, use normal mode. */
			tp->ocpl.FunctAddr = 0xFFFFFFFF;
		}
		else
		{
			int i;
			struct dev_mc_list *mclist = dev->mc_list;
			for (i=0; i< dev->mc_count; i++)
			{
				((char *)(&tp->ocpl.FunctAddr))[0] |=
					mclist->dmi_addr[2];
				((char *)(&tp->ocpl.FunctAddr))[1] |=
					mclist->dmi_addr[3];
				((char *)(&tp->ocpl.FunctAddr))[2] |=
					mclist->dmi_addr[4];
				((char *)(&tp->ocpl.FunctAddr))[3] |=
					mclist->dmi_addr[5];
				mclist = mclist->next;
			}
		}
		tms380tr_exec_cmd(dev, OC_SET_FUNCT_ADDR);
	}
	
	tp->ocpl.OPENOptions = OpenOptions;
	tms380tr_exec_cmd(dev, OC_MODIFY_OPEN_PARMS);
	return;
}

/*
 * Wait for some time (microseconds)
 */
void tms380tr_wait(unsigned long time)
{
#if 0
	long tmp;
	
	tmp = jiffies + time/(1000000/HZ);
	do {
		tmp = schedule_timeout_interruptible(tmp);
	} while(time_after(tmp, jiffies));
#else
	udelay(time);
#endif
	return;
}

/*
 * Write a command value to the SIFCMD register
 */
static void tms380tr_exec_sifcmd(struct net_device *dev, unsigned int WriteValue)
{
	unsigned short cmd;
	unsigned short SifStsValue;
	unsigned long loop_counter;

	WriteValue = ((WriteValue ^ CMD_SYSTEM_IRQ) | CMD_INTERRUPT_ADAPTER);
	cmd = (unsigned short)WriteValue;
	loop_counter = 0,5 * 800000;
	do {
		SifStsValue = SIFREADW(SIFSTS);
	} while((SifStsValue & CMD_INTERRUPT_ADAPTER) && loop_counter--);
	SIFWRITEW(cmd, SIFCMD);

	return;
}

/*
 * Processes adapter hardware reset, halts adapter and downloads firmware,
 * clears the halt bit.
 */
static int tms380tr_reset_adapter(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	unsigned short *fw_ptr;
	unsigned short count, c, count2;
	const struct firmware *fw_entry = NULL;

	if (request_firmware(&fw_entry, "tms380tr.bin", tp->pdev) != 0) {
		printk(KERN_ALERT "%s: firmware %s is missing, cannot start.\n",
			dev->name, "tms380tr.bin");
		return (-1);
	}

	fw_ptr = (unsigned short *)fw_entry->data;
	count2 = fw_entry->size / 2;

	/* Hardware adapter reset */
	SIFWRITEW(ACL_ARESET, SIFACL);
	tms380tr_wait(40);
	
	c = SIFREADW(SIFACL);
	tms380tr_wait(20);

	if(dev->dma == 0)	/* For PCI adapters */
	{
		c &= ~(ACL_NSELOUT0 | ACL_NSELOUT1);	/* Clear bits */
		if(tp->setnselout)
		  c |= (*tp->setnselout)(dev);
	}

	/* In case a command is pending - forget it */
	tp->ScbInUse = 0;

	c &= ~ACL_ARESET;		/* Clear adapter reset bit */
	c |=  ACL_CPHALT;		/* Halt adapter CPU, allow download */
	c |= ACL_BOOT;
	c |= ACL_SINTEN;
	c &= ~ACL_PSDMAEN;		/* Clear pseudo dma bit */
	SIFWRITEW(c, SIFACL);
	tms380tr_wait(40);

	count = 0;
	/* Download firmware via DIO interface: */
	do {
		if (count2 < 3) continue;

		/* Download first address part */
		SIFWRITEW(*fw_ptr, SIFADX);
		fw_ptr++;
		count2--;
		/* Download second address part */
		SIFWRITEW(*fw_ptr, SIFADD);
		fw_ptr++;
		count2--;

		if((count = *fw_ptr) != 0)	/* Load loop counter */
		{
			fw_ptr++;	/* Download block data */
			count2--;
			if (count > count2) continue;

			for(; count > 0; count--)
			{
				SIFWRITEW(*fw_ptr, SIFINC);
				fw_ptr++;
				count2--;
			}
		}
		else	/* Stop, if last block downloaded */
		{
			c = SIFREADW(SIFACL);
			c &= (~ACL_CPHALT | ACL_SINTEN);

			/* Clear CPHALT and start BUD */
			SIFWRITEW(c, SIFACL);
			if (fw_entry)
				release_firmware(fw_entry);
			return (1);
		}
	} while(count == 0);

	if (fw_entry)
		release_firmware(fw_entry);
	printk(KERN_INFO "%s: Adapter Download Failed\n", dev->name);
	return (-1);
}

/*
 * Starts bring up diagnostics of token ring adapter and evaluates
 * diagnostic results.
 */
static int tms380tr_bringup_diags(struct net_device *dev)
{
	int loop_cnt, retry_cnt;
	unsigned short Status;

	tms380tr_wait(HALF_SECOND);
	tms380tr_exec_sifcmd(dev, EXEC_SOFT_RESET);
	tms380tr_wait(HALF_SECOND);

	retry_cnt = BUD_MAX_RETRIES;	/* maximal number of retrys */

	do {
		retry_cnt--;
		if(tms380tr_debug > 3)
			printk(KERN_DEBUG "BUD-Status: ");
		loop_cnt = BUD_MAX_LOOPCNT;	/* maximum: three seconds*/
		do {			/* Inspect BUD results */
			loop_cnt--;
			tms380tr_wait(HALF_SECOND);
			Status = SIFREADW(SIFSTS);
			Status &= STS_MASK;

			if(tms380tr_debug > 3)
				printk(KERN_DEBUG " %04X \n", Status);
			/* BUD successfully completed */
			if(Status == STS_INITIALIZE)
				return (1);
		/* Unrecoverable hardware error, BUD not completed? */
		} while((loop_cnt > 0) && ((Status & (STS_ERROR | STS_TEST))
			!= (STS_ERROR | STS_TEST)));

		/* Error preventing completion of BUD */
		if(retry_cnt > 0)
		{
			printk(KERN_INFO "%s: Adapter Software Reset.\n", 
				dev->name);
			tms380tr_exec_sifcmd(dev, EXEC_SOFT_RESET);
			tms380tr_wait(HALF_SECOND);
		}
	} while(retry_cnt > 0);

	Status = SIFREADW(SIFSTS);
	
	printk(KERN_INFO "%s: Hardware error\n", dev->name);
	/* Hardware error occurred! */
	Status &= 0x001f;
	if (Status & 0x0010)
		printk(KERN_INFO "%s: BUD Error: Timeout\n", dev->name);
	else if ((Status & 0x000f) > 6)
		printk(KERN_INFO "%s: BUD Error: Illegal Failure\n", dev->name);
	else
		printk(KERN_INFO "%s: Bring Up Diagnostics Error (%04X) occurred\n", dev->name, Status & 0x000f);

	return (-1);
}

/*
 * Copy initialisation data to adapter memory, beginning at address
 * 1:0A00; Starting DMA test and evaluating result bits.
 */
static int tms380tr_init_adapter(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);

	const unsigned char SCB_Test[6] = {0x00, 0x00, 0xC1, 0xE2, 0xD4, 0x8B};
	const unsigned char SSB_Test[8] = {0xFF, 0xFF, 0xD1, 0xD7,
						0xC5, 0xD9, 0xC3, 0xD4};
	void *ptr = (void *)&tp->ipb;
	unsigned short *ipb_ptr = (unsigned short *)ptr;
	unsigned char *cb_ptr = (unsigned char *) &tp->scb;
	unsigned char *sb_ptr = (unsigned char *) &tp->ssb;
	unsigned short Status;
	int i, loop_cnt, retry_cnt;

	/* Normalize: byte order low/high, word order high/low! (only IPB!) */
	tp->ipb.SCB_Addr = SWAPW(((char *)&tp->scb - (char *)tp) + tp->dmabuffer);
	tp->ipb.SSB_Addr = SWAPW(((char *)&tp->ssb - (char *)tp) + tp->dmabuffer);

	if(tms380tr_debug > 3)
	{
		printk(KERN_DEBUG "%s: buffer (real): %lx\n", dev->name, (long) &tp->scb);
		printk(KERN_DEBUG "%s: buffer (virt): %lx\n", dev->name, (long) ((char *)&tp->scb - (char *)tp) + (long) tp->dmabuffer);
		printk(KERN_DEBUG "%s: buffer (DMA) : %lx\n", dev->name, (long) tp->dmabuffer);
		printk(KERN_DEBUG "%s: buffer (tp)  : %lx\n", dev->name, (long) tp);
	}
	/* Maximum: three initialization retries */
	retry_cnt = INIT_MAX_RETRIES;

	do {
		retry_cnt--;

		/* Transfer initialization block */
		SIFWRITEW(0x0001, SIFADX);

		/* To address 0001:0A00 of adapter RAM */
		SIFWRITEW(0x0A00, SIFADD);

		/* Write 11 words to adapter RAM */
		for(i = 0; i < 11; i++)
			SIFWRITEW(ipb_ptr[i], SIFINC);

		/* Execute SCB adapter command */
		tms380tr_exec_sifcmd(dev, CMD_EXECUTE);

		loop_cnt = INIT_MAX_LOOPCNT;	/* Maximum: 11 seconds */

		/* While remaining retries, no error and not completed */
		do {
			Status = 0;
			loop_cnt--;
			tms380tr_wait(HALF_SECOND);

			/* Mask interesting status bits */
			Status = SIFREADW(SIFSTS);
			Status &= STS_MASK;
		} while(((Status &(STS_INITIALIZE | STS_ERROR | STS_TEST)) != 0)
			&& ((Status & STS_ERROR) == 0) && (loop_cnt != 0));

		if((Status & (STS_INITIALIZE | STS_ERROR | STS_TEST)) == 0)
		{
			/* Initialization completed without error */
			i = 0;
			do {	/* Test if contents of SCB is valid */
				if(SCB_Test[i] != *(cb_ptr + i))
				{
					printk(KERN_INFO "%s: DMA failed\n", dev->name);
					/* DMA data error: wrong data in SCB */
					return (-1);
				}
				i++;
			} while(i < 6);

			i = 0;
			do {	/* Test if contents of SSB is valid */
				if(SSB_Test[i] != *(sb_ptr + i))
					/* DMA data error: wrong data in SSB */
					return (-1);
				i++;
			} while (i < 8);

			return (1);	/* Adapter successfully initialized */
		}
		else
		{
			if((Status & STS_ERROR) != 0)
			{
				/* Initialization error occurred */
				Status = SIFREADW(SIFSTS);
				Status &= STS_ERROR_MASK;
				/* ShowInitialisationErrorCode(Status); */
				printk(KERN_INFO "%s: Status error: %d\n", dev->name, Status);
				return (-1); /* Unrecoverable error */
			}
			else
			{
				if(retry_cnt > 0)
				{
					/* Reset adapter and try init again */
					tms380tr_exec_sifcmd(dev, EXEC_SOFT_RESET);
					tms380tr_wait(HALF_SECOND);
				}
			}
		}
	} while(retry_cnt > 0);

	printk(KERN_INFO "%s: Retry exceeded\n", dev->name);
	return (-1);
}

/*
 * Check for outstanding commands in command queue and tries to execute
 * command immediately. Corresponding command flag in command queue is cleared.
 */
static void tms380tr_chk_outstanding_cmds(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	unsigned long Addr = 0;

	if(tp->CMDqueue == 0)
		return;		/* No command execution */

	/* If SCB in use: no command */
	if(tp->ScbInUse == 1)
		return;

	/* Check if adapter is opened, avoiding COMMAND_REJECT
	 * interrupt by the adapter!
	 */
	if(tp->AdapterOpenFlag == 0)
	{
		if(tp->CMDqueue & OC_OPEN)
		{
			/* Execute OPEN command	*/
			tp->CMDqueue ^= OC_OPEN;

			Addr = htonl(((char *)&tp->ocpl - (char *)tp) + tp->dmabuffer);
			tp->scb.Parm[0] = LOWORD(Addr);
			tp->scb.Parm[1] = HIWORD(Addr);
			tp->scb.CMD = OPEN;
		}
		else
			/* No OPEN command queued, but adapter closed. Note:
			 * We'll try to re-open the adapter in DriverPoll()
			 */
			return;		/* No adapter command issued */
	}
	else
	{
		/* Adapter is open; evaluate command queue: try to execute
		 * outstanding commands (depending on priority!) CLOSE
		 * command queued
		 */
		if(tp->CMDqueue & OC_CLOSE)
		{
			tp->CMDqueue ^= OC_CLOSE;
			tp->AdapterOpenFlag = 0;
			tp->scb.Parm[0] = 0; /* Parm[0], Parm[1] are ignored */
			tp->scb.Parm[1] = 0; /* but should be set to zero! */
			tp->scb.CMD = CLOSE;
			if(!tp->HaltInProgress)
				tp->CMDqueue |= OC_OPEN; /* re-open adapter */
			else
				tp->CMDqueue = 0;	/* no more commands */
		}
		else
		{
			if(tp->CMDqueue & OC_RECEIVE)
			{
				tp->CMDqueue ^= OC_RECEIVE;
				Addr = htonl(((char *)tp->RplHead - (char *)tp) + tp->dmabuffer);
				tp->scb.Parm[0] = LOWORD(Addr);
				tp->scb.Parm[1] = HIWORD(Addr);
				tp->scb.CMD = RECEIVE;
			}
			else
			{
				if(tp->CMDqueue & OC_TRANSMIT_HALT)
				{
					/* NOTE: TRANSMIT.HALT must be checked 
					 * before TRANSMIT.
					 */
					tp->CMDqueue ^= OC_TRANSMIT_HALT;
					tp->scb.CMD = TRANSMIT_HALT;

					/* Parm[0] and Parm[1] are ignored
					 * but should be set to zero!
					 */
					tp->scb.Parm[0] = 0;
					tp->scb.Parm[1] = 0;
				}
				else
				{
					if(tp->CMDqueue & OC_TRANSMIT)
					{
						/* NOTE: TRANSMIT must be 
						 * checked after TRANSMIT.HALT
						 */
						if(tp->TransmitCommandActive)
						{
							if(!tp->TransmitHaltScheduled)
							{
								tp->TransmitHaltScheduled = 1;
								tms380tr_exec_cmd(dev, OC_TRANSMIT_HALT) ;
							}
							tp->TransmitCommandActive = 0;
							return;
						}

						tp->CMDqueue ^= OC_TRANSMIT;
						tms380tr_cancel_tx_queue(tp);
						Addr = htonl(((char *)tp->TplBusy - (char *)tp) + tp->dmabuffer);
						tp->scb.Parm[0] = LOWORD(Addr);
						tp->scb.Parm[1] = HIWORD(Addr);
						tp->scb.CMD = TRANSMIT;
						tp->TransmitCommandActive = 1;
					}
					else
					{
						if(tp->CMDqueue & OC_MODIFY_OPEN_PARMS)
						{
							tp->CMDqueue ^= OC_MODIFY_OPEN_PARMS;
							tp->scb.Parm[0] = tp->ocpl.OPENOptions; /* new OPEN options*/
							tp->scb.Parm[0] |= ENABLE_FULL_DUPLEX_SELECTION;
							tp->scb.Parm[1] = 0; /* is ignored but should be zero */
							tp->scb.CMD = MODIFY_OPEN_PARMS;
						}
						else
						{
							if(tp->CMDqueue & OC_SET_FUNCT_ADDR)
							{
								tp->CMDqueue ^= OC_SET_FUNCT_ADDR;
								tp->scb.Parm[0] = LOWORD(tp->ocpl.FunctAddr);
								tp->scb.Parm[1] = HIWORD(tp->ocpl.FunctAddr);
								tp->scb.CMD = SET_FUNCT_ADDR;
							}
							else
							{
								if(tp->CMDqueue & OC_SET_GROUP_ADDR)
								{
									tp->CMDqueue ^= OC_SET_GROUP_ADDR;
									tp->scb.Parm[0] = LOWORD(tp->ocpl.GroupAddr);
									tp->scb.Parm[1] = HIWORD(tp->ocpl.GroupAddr);
									tp->scb.CMD = SET_GROUP_ADDR;
								}
								else
								{
									if(tp->CMDqueue & OC_READ_ERROR_LOG)
									{
										tp->CMDqueue ^= OC_READ_ERROR_LOG;
										Addr = htonl(((char *)&tp->errorlogtable - (char *)tp) + tp->dmabuffer);
										tp->scb.Parm[0] = LOWORD(Addr);
										tp->scb.Parm[1] = HIWORD(Addr);
										tp->scb.CMD = READ_ERROR_LOG;
									}
									else
									{
										printk(KERN_WARNING "CheckForOutstandingCommand: unknown Command\n");
										tp->CMDqueue = 0;
										return;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	tp->ScbInUse = 1;	/* Set semaphore: SCB in use. */

	/* Execute SCB and generate IRQ when done. */
	tms380tr_exec_sifcmd(dev, CMD_EXECUTE | CMD_SCB_REQUEST);

	return;
}

/*
 * IRQ conditions: signal loss on the ring, transmit or receive of beacon
 * frames (disabled if bit 1 of OPEN option is set); report error MAC
 * frame transmit (disabled if bit 2 of OPEN option is set); open or short
 * circuit fault on the lobe is detected; remove MAC frame received;
 * error counter overflow (255); opened adapter is the only station in ring.
 * After some of the IRQs the adapter is closed!
 */
static void tms380tr_ring_status_irq(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);

	tp->CurrentRingStatus = be16_to_cpu((unsigned short)tp->ssb.Parm[0]);

	/* First: fill up statistics */
	if(tp->ssb.Parm[0] & SIGNAL_LOSS)
	{
		printk(KERN_INFO "%s: Signal Loss\n", dev->name);
		tp->MacStat.line_errors++;
	}

	/* Adapter is closed, but initialized */
	if(tp->ssb.Parm[0] & LOBE_WIRE_FAULT)
	{
		printk(KERN_INFO "%s: Lobe Wire Fault, Reopen Adapter\n", 
			dev->name);
		tp->MacStat.line_errors++;
	}

	if(tp->ssb.Parm[0] & RING_RECOVERY)
		printk(KERN_INFO "%s: Ring Recovery\n", dev->name);

	/* Counter overflow: read error log */
	if(tp->ssb.Parm[0] & COUNTER_OVERFLOW)
	{
		printk(KERN_INFO "%s: Counter Overflow\n", dev->name);
		tms380tr_exec_cmd(dev, OC_READ_ERROR_LOG);
	}

	/* Adapter is closed, but initialized */
	if(tp->ssb.Parm[0] & REMOVE_RECEIVED)
		printk(KERN_INFO "%s: Remove Received, Reopen Adapter\n", 
			dev->name);

	/* Adapter is closed, but initialized */
	if(tp->ssb.Parm[0] & AUTO_REMOVAL_ERROR)
		printk(KERN_INFO "%s: Auto Removal Error, Reopen Adapter\n", 
			dev->name);

	if(tp->ssb.Parm[0] & HARD_ERROR)
		printk(KERN_INFO "%s: Hard Error\n", dev->name);

	if(tp->ssb.Parm[0] & SOFT_ERROR)
		printk(KERN_INFO "%s: Soft Error\n", dev->name);

	if(tp->ssb.Parm[0] & TRANSMIT_BEACON)
		printk(KERN_INFO "%s: Transmit Beacon\n", dev->name);

	if(tp->ssb.Parm[0] & SINGLE_STATION)
		printk(KERN_INFO "%s: Single Station\n", dev->name);

	/* Check if adapter has been closed */
	if(tp->ssb.Parm[0] & ADAPTER_CLOSED)
	{
		printk(KERN_INFO "%s: Adapter closed (Reopening)," 
			"CurrentRingStat %x\n",
			dev->name, tp->CurrentRingStatus);
		tp->AdapterOpenFlag = 0;
		tms380tr_open_adapter(dev);
	}

	return;
}

/*
 * Issued if adapter has encountered an unrecoverable hardware
 * or software error.
 */
static void tms380tr_chk_irq(struct net_device *dev)
{
	int i;
	unsigned short AdapterCheckBlock[4];
	struct net_local *tp = netdev_priv(dev);

	tp->AdapterOpenFlag = 0;	/* Adapter closed now */

	/* Page number of adapter memory */
	SIFWRITEW(0x0001, SIFADX);
	/* Address offset */
	SIFWRITEW(CHECKADDR, SIFADR);

	/* Reading 8 byte adapter check block. */
	for(i = 0; i < 4; i++)
		AdapterCheckBlock[i] = SIFREADW(SIFINC);

	if(tms380tr_debug > 3)
	{
		printk(KERN_DEBUG "%s: AdapterCheckBlock: ", dev->name);
		for (i = 0; i < 4; i++)
			printk("%04X", AdapterCheckBlock[i]);
		printk("\n");
	}

	switch(AdapterCheckBlock[0])
	{
		case DIO_PARITY:
			printk(KERN_INFO "%s: DIO parity error\n", dev->name);
			break;

		case DMA_READ_ABORT:
			printk(KERN_INFO "%s DMA read operation aborted:\n",
				dev->name);
			switch (AdapterCheckBlock[1])
			{
				case 0:
					printk(KERN_INFO "Timeout\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2],
						AdapterCheckBlock[3]);
					break;

				case 1:
					printk(KERN_INFO "Parity error\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2], 
						AdapterCheckBlock[3]);
					break;

				case 2: 
					printk(KERN_INFO "Bus error\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2], 
						AdapterCheckBlock[3]);
					break;

				default:
					printk(KERN_INFO "Unknown error.\n");
					break;
			}
			break;

		case DMA_WRITE_ABORT:
			printk(KERN_INFO "%s: DMA write operation aborted: \n",
				dev->name);
			switch (AdapterCheckBlock[1])
			{
				case 0: 
					printk(KERN_INFO "Timeout\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2], 
						AdapterCheckBlock[3]);
					break;

				case 1: 
					printk(KERN_INFO "Parity error\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2], 
						AdapterCheckBlock[3]);
					break;

				case 2: 
					printk(KERN_INFO "Bus error\n");
					printk(KERN_INFO "Address: %04X %04X\n",
						AdapterCheckBlock[2], 
						AdapterCheckBlock[3]);
					break;

				default:
					printk(KERN_INFO "Unknown error.\n");
					break;
			}
			break;

		case ILLEGAL_OP_CODE:
			printk(KERN_INFO "%s: Illegal operation code in firmware\n",
				dev->name);
			/* Parm[0-3]: adapter internal register R13-R15 */
			break;

		case PARITY_ERRORS:
			printk(KERN_INFO "%s: Adapter internal bus parity error\n",
				dev->name);
			/* Parm[0-3]: adapter internal register R13-R15 */
			break;

		case RAM_DATA_ERROR:
			printk(KERN_INFO "%s: RAM data error\n", dev->name);
			/* Parm[0-1]: MSW/LSW address of RAM location. */
			break;

		case RAM_PARITY_ERROR:
			printk(KERN_INFO "%s: RAM parity error\n", dev->name);
			/* Parm[0-1]: MSW/LSW address of RAM location. */
			break;

		case RING_UNDERRUN:
			printk(KERN_INFO "%s: Internal DMA underrun detected\n",
				dev->name);
			break;

		case INVALID_IRQ:
			printk(KERN_INFO "%s: Unrecognized interrupt detected\n",
				dev->name);
			/* Parm[0-3]: adapter internal register R13-R15 */
			break;

		case INVALID_ERROR_IRQ:
			printk(KERN_INFO "%s: Unrecognized error interrupt detected\n",
				dev->name);
			/* Parm[0-3]: adapter internal register R13-R15 */
			break;

		case INVALID_XOP:
			printk(KERN_INFO "%s: Unrecognized XOP request detected\n",
				dev->name);
			/* Parm[0-3]: adapter internal register R13-R15 */
			break;

		default:
			printk(KERN_INFO "%s: Unknown status", dev->name);
			break;
	}

	if(tms380tr_chipset_init(dev) == 1)
	{
		/* Restart of firmware successful */
		tp->AdapterOpenFlag = 1;
	}

	return;
}

/*
 * Internal adapter pointer to RAM data are copied from adapter into
 * host system.
 */
static int tms380tr_read_ptr(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	unsigned short adapterram;

	tms380tr_read_ram(dev, (unsigned char *)&tp->intptrs.BurnedInAddrPtr,
			ADAPTER_INT_PTRS, 16);
	tms380tr_read_ram(dev, (unsigned char *)&adapterram,
			cpu_to_be16((unsigned short)tp->intptrs.AdapterRAMPtr), 2);
	return be16_to_cpu(adapterram); 
}

/*
 * Reads a number of bytes from adapter to system memory.
 */
static void tms380tr_read_ram(struct net_device *dev, unsigned char *Data,
				unsigned short Address, int Length)
{
	int i;
	unsigned short old_sifadx, old_sifadr, InWord;

	/* Save the current values */
	old_sifadx = SIFREADW(SIFADX);
	old_sifadr = SIFREADW(SIFADR);

	/* Page number of adapter memory */
	SIFWRITEW(0x0001, SIFADX);
	/* Address offset in adapter RAM */
        SIFWRITEW(Address, SIFADR);

	/* Copy len byte from adapter memory to system data area. */
	i = 0;
	for(;;)
	{
		InWord = SIFREADW(SIFINC);

		*(Data + i) = HIBYTE(InWord);	/* Write first byte */
		if(++i == Length)		/* All is done break */
			break;

		*(Data + i) = LOBYTE(InWord);	/* Write second byte */
		if (++i == Length)		/* All is done break */
			break;
	}

	/* Restore original values */
	SIFWRITEW(old_sifadx, SIFADX);
	SIFWRITEW(old_sifadr, SIFADR);

	return;
}

/*
 * Cancel all queued packets in the transmission queue.
 */
static void tms380tr_cancel_tx_queue(struct net_local* tp)
{
	TPL *tpl;

	/*
	 * NOTE: There must not be an active TRANSMIT command pending, when
	 * this function is called.
	 */
	if(tp->TransmitCommandActive)
		return;

	for(;;)
	{
		tpl = tp->TplBusy;
		if(!tpl->BusyFlag)
			break;
		/* "Remove" TPL from busy list. */
		tp->TplBusy = tpl->NextTPLPtr;
		tms380tr_write_tpl_status(tpl, 0);	/* Clear VALID bit */
		tpl->BusyFlag = 0;		/* "free" TPL */

		printk(KERN_INFO "Cancel tx (%08lXh).\n", (unsigned long)tpl);
		if (tpl->DMABuff)
			dma_unmap_single(tp->pdev, tpl->DMABuff, tpl->Skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(tpl->Skb);
	}

	return;
}

/*
 * This function is called whenever a transmit interrupt is generated by the
 * adapter. For a command complete interrupt, it is checked if we have to
 * issue a new transmit command or not.
 */
static void tms380tr_tx_status_irq(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	unsigned char HighByte, HighAc, LowAc;
	TPL *tpl;

	/* NOTE: At this point the SSB from TRANSMIT STATUS is no longer
	 * available, because the CLEAR SSB command has already been issued.
	 *
	 * Process all complete transmissions.
	 */

	for(;;)
	{
		tpl = tp->TplBusy;
		if(!tpl->BusyFlag || (tpl->Status
			& (TX_VALID | TX_FRAME_COMPLETE))
			!= TX_FRAME_COMPLETE)
		{
			break;
		}

		/* "Remove" TPL from busy list. */
		tp->TplBusy = tpl->NextTPLPtr ;

		/* Check the transmit status field only for directed frames*/
		if(DIRECTED_FRAME(tpl) && (tpl->Status & TX_ERROR) == 0)
		{
			HighByte = GET_TRANSMIT_STATUS_HIGH_BYTE(tpl->Status);
			HighAc   = GET_FRAME_STATUS_HIGH_AC(HighByte);
			LowAc    = GET_FRAME_STATUS_LOW_AC(HighByte);

			if((HighAc != LowAc) || (HighAc == AC_NOT_RECOGNIZED))
			{
				printk(KERN_DEBUG "%s: (DA=%08lX not recognized)\n",
					dev->name,
					*(unsigned long *)&tpl->MData[2+2]);
			}
			else
			{
				if(tms380tr_debug > 3)
					printk(KERN_DEBUG "%s: Directed frame tx'd\n", 
						dev->name);
			}
		}
		else
		{
			if(!DIRECTED_FRAME(tpl))
			{
				if(tms380tr_debug > 3)
					printk(KERN_DEBUG "%s: Broadcast frame tx'd\n",
						dev->name);
			}
		}

		tp->MacStat.tx_packets++;
		if (tpl->DMABuff)
			dma_unmap_single(tp->pdev, tpl->DMABuff, tpl->Skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_irq(tpl->Skb);
		tpl->BusyFlag = 0;	/* "free" TPL */
	}

	if(!tp->TplFree->NextTPLPtr->BusyFlag)
		netif_wake_queue(dev);
	return;
}

/*
 * Called if a frame receive interrupt is generated by the adapter.
 * Check if the frame is valid and indicate it to system.
 */
static void tms380tr_rcv_status_irq(struct net_device *dev)
{
	struct net_local *tp = netdev_priv(dev);
	unsigned char *ReceiveDataPtr;
	struct sk_buff *skb;
	unsigned int Length, Length2;
	RPL *rpl;
	RPL *SaveHead;
	dma_addr_t dmabuf;

	/* NOTE: At this point the SSB from RECEIVE STATUS is no longer
	 * available, because the CLEAR SSB command has already been issued.
	 *
	 * Process all complete receives.
	 */

	for(;;)
	{
		rpl = tp->RplHead;
		if(rpl->Status & RX_VALID)
			break;		/* RPL still in use by adapter */

		/* Forward RPLHead pointer to next list. */
		SaveHead = tp->RplHead;
		tp->RplHead = rpl->NextRPLPtr;

		/* Get the frame size (Byte swap for Intel).
		 * Do this early (see workaround comment below)
		 */
		Length = be16_to_cpu(rpl->FrameSize);

		/* Check if the Frame_Start, Frame_End and
		 * Frame_Complete bits are set.
		 */
		if((rpl->Status & VALID_SINGLE_BUFFER_FRAME)
			== VALID_SINGLE_BUFFER_FRAME)
		{
			ReceiveDataPtr = rpl->MData;

			/* Workaround for delayed write of FrameSize on ISA
			 * (FrameSize is false but valid-bit is reset)
			 * Frame size is set to zero when the RPL is freed.
			 * Length2 is there because there have also been
			 * cases where the FrameSize was partially written
			 */
			Length2 = be16_to_cpu(rpl->FrameSize);

			if(Length == 0 || Length != Length2)
			{
				tp->RplHead = SaveHead;
				break;	/* Return to tms380tr_interrupt */
			}
			tms380tr_update_rcv_stats(tp,ReceiveDataPtr,Length);
			  
			if(tms380tr_debug > 3)
				printk(KERN_DEBUG "%s: Packet Length %04X (%d)\n",
					dev->name, Length, Length);
			  
			/* Indicate the received frame to system the
			 * adapter does the Source-Routing padding for 
			 * us. See: OpenOptions in tms380tr_init_opb()
			 */
			skb = rpl->Skb;
			if(rpl->SkbStat == SKB_UNAVAILABLE)
			{
				/* Try again to allocate skb */
				skb = dev_alloc_skb(tp->MaxPacketSize);
				if(skb == NULL)
				{
					/* Update Stats ?? */
				}
				else
				{
					skb_put(skb, tp->MaxPacketSize);
					rpl->SkbStat 	= SKB_DATA_COPY;
					ReceiveDataPtr 	= rpl->MData;
				}
			}

			if(skb && (rpl->SkbStat == SKB_DATA_COPY
				|| rpl->SkbStat == SKB_DMA_DIRECT))
			{
				if(rpl->SkbStat == SKB_DATA_COPY)
					skb_copy_to_linear_data(skb, ReceiveDataPtr,
						       Length);

				/* Deliver frame to system */
				rpl->Skb = NULL;
				skb_trim(skb,Length);
				skb->protocol = tr_type_trans(skb,dev);
				netif_rx(skb);
				dev->last_rx = jiffies;
			}
		}
		else	/* Invalid frame */
		{
			if(rpl->Skb != NULL)
				dev_kfree_skb_irq(rpl->Skb);

			/* Skip list. */
			if(rpl->Status & RX_START_FRAME)
				/* Frame start bit is set -> overflow. */
				tp->MacStat.rx_errors++;
		}
		if (rpl->DMABuff)
			dma_unmap_single(tp->pdev, rpl->DMABuff, tp->MaxPacketSize, DMA_TO_DEVICE);
		rpl->DMABuff = 0;

		/* Allocate new skb for rpl */
		rpl->Skb = dev_alloc_skb(tp->MaxPacketSize);
		/* skb == NULL ? then use local buffer */
		if(rpl->Skb == NULL)
		{
			rpl->SkbStat = SKB_UNAVAILABLE;
			rpl->FragList[0].DataAddr = htonl(((char *)tp->LocalRxBuffers[rpl->RPLIndex] - (char *)tp) + tp->dmabuffer);
			rpl->MData = tp->LocalRxBuffers[rpl->RPLIndex];
		}
		else	/* skb != NULL */
		{
			rpl->Skb->dev = dev;
			skb_put(rpl->Skb, tp->MaxPacketSize);

			/* Data unreachable for DMA ? then use local buffer */
			dmabuf = dma_map_single(tp->pdev, rpl->Skb->data, tp->MaxPacketSize, DMA_FROM_DEVICE);
			if(tp->dmalimit && (dmabuf + tp->MaxPacketSize > tp->dmalimit))
			{
				rpl->SkbStat = SKB_DATA_COPY;
				rpl->FragList[0].DataAddr = htonl(((char *)tp->LocalRxBuffers[rpl->RPLIndex] - (char *)tp) + tp->dmabuffer);
				rpl->MData = tp->LocalRxBuffers[rpl->RPLIndex];
			}
			else
			{
				/* DMA directly in skb->data */
				rpl->SkbStat = SKB_DMA_DIRECT;
				rpl->FragList[0].DataAddr = htonl(dmabuf);
				rpl->MData = rpl->Skb->data;
				rpl->DMABuff = dmabuf;
			}
		}

		rpl->FragList[0].DataCount = cpu_to_be16((unsigned short)tp->MaxPacketSize);
		rpl->FrameSize = 0;

		/* Pass the last RPL back to the adapter */
		tp->RplTail->FrameSize = 0;

		/* Reset the CSTAT field in the list. */
		tms380tr_write_rpl_status(tp->RplTail, RX_VALID | RX_FRAME_IRQ);

		/* Current RPL becomes last one in list. */
		tp->RplTail = tp->RplTail->NextRPLPtr;

		/* Inform adapter about RPL valid. */
		tms380tr_exec_sifcmd(dev, CMD_RX_VALID);
	}

	return;
}

/*
 * This function should be used whenever the status of any RPL must be
 * modified by the driver, because the compiler may otherwise change the
 * order of instructions such that writing the RPL status may be executed
 * at an undesireable time. When this function is used, the status is
 * always written when the function is called.
 */
static void tms380tr_write_rpl_status(RPL *rpl, unsigned int Status)
{
	rpl->Status = Status;

	return;
}

/*
 * The function updates the statistic counters in mac->MacStat.
 * It differtiates between directed and broadcast/multicast ( ==functional)
 * frames.
 */
static void tms380tr_update_rcv_stats(struct net_local *tp, unsigned char DataPtr[],
					unsigned int Length)
{
	tp->MacStat.rx_packets++;
	tp->MacStat.rx_bytes += Length;
	
	/* Test functional bit */
	if(DataPtr[2] & GROUP_BIT)
		tp->MacStat.multicast++;

	return;
}

static int tms380tr_set_mac_address(struct net_device *dev, void *addr)
{
	struct net_local *tp = netdev_priv(dev);
	struct sockaddr *saddr = addr;
	
	if (tp->AdapterOpenFlag || tp->AdapterVirtOpenFlag) {
		printk(KERN_WARNING "%s: Cannot set MAC/LAA address while card is open\n", dev->name);
		return -EIO;
	}
	memcpy(dev->dev_addr, saddr->sa_data, dev->addr_len);
	return 0;
}

#if TMS380TR_DEBUG > 0
/*
 * Dump Packet (data)
 */
static void tms380tr_dump(unsigned char *Data, int length)
{
	int i, j;

	for (i = 0, j = 0; i < length / 8; i++, j += 8)
	{
		printk(KERN_DEBUG "%02x %02x %02x %02x %02x %02x %02x %02x\n",
		       Data[j+0],Data[j+1],Data[j+2],Data[j+3],
		       Data[j+4],Data[j+5],Data[j+6],Data[j+7]);
	}

	return;
}
#endif

void tmsdev_term(struct net_device *dev)
{
	struct net_local *tp;

	tp = netdev_priv(dev);
	dma_unmap_single(tp->pdev, tp->dmabuffer, sizeof(struct net_local),
		DMA_BIDIRECTIONAL);
}

int tmsdev_init(struct net_device *dev, struct device *pdev)
{
	struct net_local *tms_local;

	memset(dev->priv, 0, sizeof(struct net_local));
	tms_local = netdev_priv(dev);
	init_waitqueue_head(&tms_local->wait_for_tok_int);
	if (pdev->dma_mask)
		tms_local->dmalimit = *pdev->dma_mask;
	else
		return -ENOMEM;
	tms_local->pdev = pdev;
	tms_local->dmabuffer = dma_map_single(pdev, (void *)tms_local,
	    sizeof(struct net_local), DMA_BIDIRECTIONAL);
	if (tms_local->dmabuffer + sizeof(struct net_local) > 
			tms_local->dmalimit)
	{
		printk(KERN_INFO "%s: Memory not accessible for DMA\n",
			dev->name);
		tmsdev_term(dev);
		return -ENOMEM;
	}
	
	/* These can be overridden by the card driver if needed */
	dev->open		= tms380tr_open;
	dev->stop		= tms380tr_close;
	dev->do_ioctl		= NULL; 
	dev->hard_start_xmit	= tms380tr_send_packet;
	dev->tx_timeout		= tms380tr_timeout;
	dev->watchdog_timeo	= HZ;
	dev->get_stats		= tms380tr_get_stats;
	dev->set_multicast_list = &tms380tr_set_multicast_list;
	dev->set_mac_address	= tms380tr_set_mac_address;

	return 0;
}

EXPORT_SYMBOL(tms380tr_open);
EXPORT_SYMBOL(tms380tr_close);
EXPORT_SYMBOL(tms380tr_interrupt);
EXPORT_SYMBOL(tmsdev_init);
EXPORT_SYMBOL(tmsdev_term);
EXPORT_SYMBOL(tms380tr_wait);

#ifdef MODULE

static struct module *TMS380_module = NULL;

int init_module(void)
{
	printk(KERN_DEBUG "%s", version);
	
	TMS380_module = &__this_module;
	return 0;
}

void cleanup_module(void)
{
	TMS380_module = NULL;
}
#endif

MODULE_LICENSE("GPL");

