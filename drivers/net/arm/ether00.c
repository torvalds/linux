/*
 *  drivers/net/ether00.c
 *
 *  Copyright (C) 2001 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* includes */
#include <linux/config.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/tqueue.h>
#include <linux/mtd/mtd.h>
#include <linux/pld/pld_hotswap.h>
#include <asm/arch/excalibur.h>
#include <asm/arch/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sizes.h>

#include <asm/arch/ether00.h>
#include <asm/arch/tdkphy.h>


MODULE_AUTHOR("Clive Davies");
MODULE_DESCRIPTION("Altera Ether00 IP core driver");
MODULE_LICENSE("GPL");

#define PKT_BUF_SZ 1540 /* Size of each rx buffer */
#define ETH_NR 4 /* Number of MACs this driver supports */

#define DEBUG(x)

#define __dma_va(x) (unsigned int)((unsigned int)priv->dma_data+(((unsigned int)(x))&(EXC_SPSRAM_BLOCK0_SIZE-1)))
#define __dma_pa(x) (unsigned int)(EXC_SPSRAM_BLOCK0_BASE+(((unsigned int)(x))-(unsigned int)priv->dma_data))

#define ETHER00_BASE	0
#define	ETHER00_TYPE
#define ETHER00_NAME "ether00"
#define MAC_REG_SIZE 0x400 /* size of MAC register area */



/* typedefs */

/* The definition of the driver control structure */

#define RX_NUM_BUFF     10
#define RX_NUM_FDESC    10
#define TX_NUM_FDESC    10

struct tx_fda_ent{
	FDA_DESC  fd;
	BUF_DESC  bd;
	BUF_DESC  pad;
};
struct rx_fda_ent{
	FDA_DESC  fd;
	BUF_DESC  bd;
	BUF_DESC  pad;
};
struct rx_blist_ent{
	FDA_DESC  fd;
	BUF_DESC  bd;
	BUF_DESC  pad;
};
struct net_priv
{
	struct net_device_stats stats;
	struct sk_buff* skb;
	void* dma_data;
	struct rx_blist_ent*  rx_blist_vp;
	struct rx_fda_ent* rx_fda_ptr;
	struct tx_fda_ent* tx_fdalist_vp;
	struct tq_struct  tq_memupdate;
	unsigned char   memupdate_scheduled;
	unsigned char   rx_disabled;
	unsigned char   queue_stopped;
	spinlock_t rx_lock;
};

static const char vendor_id[2]={0x07,0xed};

#ifdef ETHER00_DEBUG

/* Dump (most) registers for debugging puposes */

static void dump_regs(struct net_device *dev){
	struct net_priv* priv=dev->priv;
	unsigned int* i;

	printk("\n RX free descriptor area:\n");

	for(i=(unsigned int*)priv->rx_fda_ptr;
	    i<((unsigned int*)(priv->rx_fda_ptr+RX_NUM_FDESC));){
		printk("%#8x %#8x %#8x %#8x\n",*i,*(i+1),*(i+2),*(i+3));
		i+=4;
	}

	printk("\n RX buffer list:\n");

	for(i=(unsigned int*)priv->rx_blist_vp;
	    i<((unsigned int*)(priv->rx_blist_vp+RX_NUM_BUFF));){
		printk("%#8x %#8x %#8x %#8x\n",*i,*(i+1),*(i+2),*(i+3));
		i+=4;
	}

	printk("\n TX frame descriptor list:\n");

	for(i=(unsigned int*)priv->tx_fdalist_vp;
	    i<((unsigned int*)(priv->tx_fdalist_vp+TX_NUM_FDESC));){
		printk("%#8x %#8x %#8x %#8x\n",*i,*(i+1),*(i+2),*(i+3));
		i+=4;
	}

	printk("\ndma ctl=%#x\n",readw(ETHER_DMA_CTL(dev->base_addr)));
	printk("txfrmptr=%#x\n",readw(ETHER_TXFRMPTR(dev->base_addr)));
	printk("txthrsh=%#x\n",readw(ETHER_TXTHRSH(dev->base_addr)));
	printk("txpollctr=%#x\n",readw(ETHER_TXPOLLCTR(dev->base_addr)));
	printk("blfrmptr=%#x\n",readw(ETHER_BLFRMPTR(dev->base_addr)));
	printk("rxfragsize=%#x\n",readw(ETHER_RXFRAGSIZE(dev->base_addr)));
	printk("tx_int_en=%#x\n",readw(ETHER_INT_EN(dev->base_addr)));
	printk("fda_bas=%#x\n",readw(ETHER_FDA_BAS(dev->base_addr)));
	printk("fda_lim=%#x\n",readw(ETHER_FDA_LIM(dev->base_addr)));
	printk("int_src=%#x\n",readw(ETHER_INT_SRC(dev->base_addr)));
	printk("pausecnt=%#x\n",readw(ETHER_PAUSECNT(dev->base_addr)));
	printk("rempaucnt=%#x\n",readw(ETHER_REMPAUCNT(dev->base_addr)));
	printk("txconfrmstat=%#x\n",readw(ETHER_TXCONFRMSTAT(dev->base_addr)));
	printk("mac_ctl=%#x\n",readw(ETHER_MAC_CTL(dev->base_addr)));
	printk("arc_ctl=%#x\n",readw(ETHER_ARC_CTL(dev->base_addr)));
	printk("tx_ctl=%#x\n",readw(ETHER_TX_CTL(dev->base_addr)));
}
#endif /* ETHER00_DEBUG */


static int ether00_write_phy(struct net_device *dev, short address, short value)
{
	volatile int count = 1024;
	writew(value,ETHER_MD_DATA(dev->base_addr));
	writew( ETHER_MD_CA_BUSY_MSK |
		ETHER_MD_CA_WR_MSK |
		(address & ETHER_MD_CA_ADDR_MSK),
		ETHER_MD_CA(dev->base_addr));

	/* Wait for the command to complete */
	while((readw(ETHER_MD_CA(dev->base_addr)) & ETHER_MD_CA_BUSY_MSK)&&count){
		count--;
	}
	if (!count){
		printk("Write to phy failed, addr=%#x, data=%#x\n",address, value);
		return -EIO;
	}
	return 0;
}

static int ether00_read_phy(struct net_device *dev, short address)
{
	volatile int count = 1024;
	writew( ETHER_MD_CA_BUSY_MSK |
		(address & ETHER_MD_CA_ADDR_MSK),
		ETHER_MD_CA(dev->base_addr));

	/* Wait for the command to complete */
	while((readw(ETHER_MD_CA(dev->base_addr)) & ETHER_MD_CA_BUSY_MSK)&&count){
		count--;
	}
	if (!count){
		printk(KERN_WARNING "Read from phy timed out\n");
		return -EIO;
	}
	return readw(ETHER_MD_DATA(dev->base_addr));
}

static void ether00_phy_int(int irq_num, void* dev_id, struct pt_regs* regs)
{
	struct net_device* dev=dev_id;
	int irq_status;

	irq_status=ether00_read_phy(dev, PHY_IRQ_CONTROL);

	if(irq_status & PHY_IRQ_CONTROL_ANEG_COMP_INT_MSK){
		/*
		 * Autonegotiation complete on epxa10db. The mac doesn't
		 * twig if we're in full duplex so we need to check the
		 * phy status register and configure the mac accordingly
		 */
		if(ether00_read_phy(dev, PHY_STATUS)&(PHY_STATUS_10T_F_MSK|PHY_STATUS_100_X_F_MSK)){
			int tmp;
			tmp=readl(ETHER_MAC_CTL(dev->base_addr));
			writel(tmp|ETHER_MAC_CTL_FULLDUP_MSK,ETHER_MAC_CTL(dev->base_addr));
		}
	}

	if(irq_status&PHY_IRQ_CONTROL_LS_CHG_INT_MSK){

		if(ether00_read_phy(dev, PHY_STATUS)& PHY_STATUS_LINK_MSK){
			/* Link is up */
			netif_carrier_on(dev);
			//printk("Carrier on\n");
		}else{
			netif_carrier_off(dev);
			//printk("Carrier off\n");

		}
	}

}

static void setup_blist_entry(struct sk_buff* skb,struct rx_blist_ent* blist_ent_ptr){
	/* Make the buffer consistent with the cache as the mac is going to write
	 * directly into it*/
	blist_ent_ptr->fd.FDSystem=(unsigned int)skb;
	blist_ent_ptr->bd.BuffData=(char*)__pa(skb->data);
	consistent_sync(skb->data,PKT_BUF_SZ,PCI_DMA_FROMDEVICE);
        /* align IP on 16 Byte (DMA_CTL set to skip 2 bytes) */
	skb_reserve(skb,2);
	blist_ent_ptr->bd.BuffLength=PKT_BUF_SZ-2;
	blist_ent_ptr->fd.FDLength=1;
	blist_ent_ptr->fd.FDCtl=FDCTL_COWNSFD_MSK;
	blist_ent_ptr->bd.BDCtl=BDCTL_COWNSBD_MSK;
}


static int ether00_mem_init(struct net_device* dev)
{
	struct net_priv* priv=dev->priv;
	struct tx_fda_ent *tx_fd_ptr,*tx_end_ptr;
	struct rx_blist_ent* blist_ent_ptr;
	int i;

	/*
	 * Grab a block of on chip SRAM to contain the control stuctures for
	 * the ethernet MAC. This uncached becuase it needs to be accesses by both
	 * bus masters (cpu + mac). However, it shouldn't matter too much in terms
	 * of speed as its on chip memory
	 */
	priv->dma_data=ioremap_nocache(EXC_SPSRAM_BLOCK0_BASE,EXC_SPSRAM_BLOCK0_SIZE );
	if (!priv->dma_data)
		return -ENOMEM;

	priv->rx_fda_ptr=(struct rx_fda_ent*)priv->dma_data;
	/*
	 * Now share it out amongst the Frame descriptors and the buffer list
	 */
	priv->rx_blist_vp=(struct rx_blist_ent*)((unsigned int)priv->dma_data+RX_NUM_FDESC*sizeof(struct rx_fda_ent));

	/*
	 *Initalise the FDA list
	 */
	/* set ownership to the controller */
	memset(priv->rx_fda_ptr,0x80,RX_NUM_FDESC*sizeof(struct rx_fda_ent));

	/*
	 *Initialise the buffer list
	 */
	blist_ent_ptr=priv->rx_blist_vp;
	i=0;
	while(blist_ent_ptr<(priv->rx_blist_vp+RX_NUM_BUFF)){
		struct sk_buff *skb;
		blist_ent_ptr->fd.FDLength=1;
		skb=dev_alloc_skb(PKT_BUF_SZ);
		if(skb){
			setup_blist_entry(skb,blist_ent_ptr);
			blist_ent_ptr->fd.FDNext=(FDA_DESC*)__dma_pa(blist_ent_ptr+1);
			blist_ent_ptr->bd.BDStat=i++;
			blist_ent_ptr++;
		}
		else
		{
			printk("Failed to initalise buffer list\n");
		}

	}
	blist_ent_ptr--;
	blist_ent_ptr->fd.FDNext=(FDA_DESC*)__dma_pa(priv->rx_blist_vp);

	priv->tx_fdalist_vp=(struct tx_fda_ent*)(priv->rx_blist_vp+RX_NUM_BUFF);

	/* Initialise the buffers to be a circular list. The mac will then go poll
	 * the list until it finds a frame ready to transmit */
	tx_end_ptr=priv->tx_fdalist_vp+TX_NUM_FDESC;
	for(tx_fd_ptr=priv->tx_fdalist_vp;tx_fd_ptr<tx_end_ptr;tx_fd_ptr++){
		tx_fd_ptr->fd.FDNext=(FDA_DESC*)__dma_pa((tx_fd_ptr+1));
		tx_fd_ptr->fd.FDCtl=1;
		tx_fd_ptr->fd.FDStat=0;
		tx_fd_ptr->fd.FDLength=1;

	}
	/* Change the last FDNext pointer to make a circular list */
	tx_fd_ptr--;
	tx_fd_ptr->fd.FDNext=(FDA_DESC*)__dma_pa(priv->tx_fdalist_vp);

	/* Point the device at the chain of Rx and Tx Buffers */
	writel((unsigned int)__dma_pa(priv->rx_fda_ptr),ETHER_FDA_BAS(dev->base_addr));
	writel((RX_NUM_FDESC-1)*sizeof(struct rx_fda_ent),ETHER_FDA_LIM(dev->base_addr));
	writel((unsigned int)__dma_pa(priv->rx_blist_vp),ETHER_BLFRMPTR(dev->base_addr));

	writel((unsigned int)__dma_pa(priv->tx_fdalist_vp),ETHER_TXFRMPTR(dev->base_addr));

	return 0;
}


void ether00_mem_update(void* dev_id)
{
	struct net_device* dev=dev_id;
	struct net_priv* priv=dev->priv;
	struct sk_buff* skb;
	struct tx_fda_ent *fda_ptr=priv->tx_fdalist_vp;
	struct rx_blist_ent* blist_ent_ptr;
	unsigned long flags;

	priv->tq_memupdate.sync=0;
	//priv->tq_memupdate.list=
	priv->memupdate_scheduled=0;

	/* Transmit interrupt */
	while(fda_ptr<(priv->tx_fdalist_vp+TX_NUM_FDESC)){
		if(!(FDCTL_COWNSFD_MSK&fda_ptr->fd.FDCtl) && (ETHER_TX_STAT_COMP_MSK&fda_ptr->fd.FDStat)){
			priv->stats.tx_packets++;
			priv->stats.tx_bytes+=fda_ptr->bd.BuffLength;
			skb=(struct sk_buff*)fda_ptr->fd.FDSystem;
			//printk("%d:txcln:fda=%#x skb=%#x\n",jiffies,fda_ptr,skb);
			dev_kfree_skb(skb);
			fda_ptr->fd.FDSystem=0;
			fda_ptr->fd.FDStat=0;
			fda_ptr->fd.FDCtl=0;
		}
		fda_ptr++;
	}
	/* Fill in any missing buffers from the received queue */
	spin_lock_irqsave(&priv->rx_lock,flags);
	blist_ent_ptr=priv->rx_blist_vp;
	while(blist_ent_ptr<(priv->rx_blist_vp+RX_NUM_BUFF)){
		/* fd.FDSystem of 0 indicates we failed to allocate the buffer in the ISR */
		if(!blist_ent_ptr->fd.FDSystem){
			struct sk_buff *skb;
			skb=dev_alloc_skb(PKT_BUF_SZ);
			blist_ent_ptr->fd.FDSystem=(unsigned int)skb;
			if(skb){
				setup_blist_entry(skb,blist_ent_ptr);
			}
			else
			{
				break;
			}
		}
		blist_ent_ptr++;
	}
	spin_unlock_irqrestore(&priv->rx_lock,flags);
	if(priv->queue_stopped){
		//printk("%d:cln:start q\n",jiffies);
		netif_start_queue(dev);
	}
	if(priv->rx_disabled){
		//printk("%d:enable_irq\n",jiffies);
		priv->rx_disabled=0;
		writel(ETHER_RX_CTL_RXEN_MSK,ETHER_RX_CTL(dev->base_addr));

	}
}


static void ether00_int( int irq_num, void* dev_id, struct pt_regs* regs)
{
	struct net_device* dev=dev_id;
	struct net_priv* priv=dev->priv;

	unsigned int   interruptValue;

	interruptValue=readl(ETHER_INT_SRC(dev->base_addr));

	//printk("INT_SRC=%x\n",interruptValue);

	if(!(readl(ETHER_INT_SRC(dev->base_addr)) & ETHER_INT_SRC_IRQ_MSK))
	{
		return;		/* Interrupt wasn't caused by us!! */
	}

	if(readl(ETHER_INT_SRC(dev->base_addr))&
	   (ETHER_INT_SRC_INTMACRX_MSK |
	    ETHER_INT_SRC_FDAEX_MSK |
	    ETHER_INT_SRC_BLEX_MSK)) {
		struct rx_blist_ent* blist_ent_ptr;
		struct rx_fda_ent* fda_ent_ptr;
		struct sk_buff* skb;

		fda_ent_ptr=priv->rx_fda_ptr;
		spin_lock(&priv->rx_lock);
		while(fda_ent_ptr<(priv->rx_fda_ptr+RX_NUM_FDESC)){
			int result;

			if(!(fda_ent_ptr->fd.FDCtl&FDCTL_COWNSFD_MSK))
			{
				/* This frame is ready for processing */
				/*find the corresponding buffer in the bufferlist */
				blist_ent_ptr=priv->rx_blist_vp+fda_ent_ptr->bd.BDStat;
				skb=(struct sk_buff*)blist_ent_ptr->fd.FDSystem;

				/* Pass this skb up the stack */
				skb->dev=dev;
				skb_put(skb,fda_ent_ptr->fd.FDLength);
				skb->protocol=eth_type_trans(skb,dev);
				skb->ip_summed=CHECKSUM_UNNECESSARY;
				result=netif_rx(skb);
				/* Update statistics */
				priv->stats.rx_packets++;
				priv->stats.rx_bytes+=fda_ent_ptr->fd.FDLength;

				/* Free the FDA entry */
				fda_ent_ptr->bd.BDStat=0xff;
				fda_ent_ptr->fd.FDCtl=FDCTL_COWNSFD_MSK;

				/* Allocate a new skb and point the bd entry to it */
				blist_ent_ptr->fd.FDSystem=0;
				skb=dev_alloc_skb(PKT_BUF_SZ);
				//printk("allocskb=%#x\n",skb);
				if(skb){
					setup_blist_entry(skb,blist_ent_ptr);

				}
				else if(!priv->memupdate_scheduled){
					int tmp;
					/* There are no buffers at the moment, so schedule */
					/* the background task to sort this out */
					schedule_task(&priv->tq_memupdate);
					priv->memupdate_scheduled=1;
					printk(KERN_DEBUG "%s:No buffers",dev->name);
					/* If this interrupt was due to a lack of buffers then
					 * we'd better stop the receiver too */
					if(interruptValue&ETHER_INT_SRC_BLEX_MSK){
						priv->rx_disabled=1;
						tmp=readl(ETHER_INT_SRC(dev->base_addr));
						writel(tmp&~ETHER_RX_CTL_RXEN_MSK,ETHER_RX_CTL(dev->base_addr));
						printk(KERN_DEBUG "%s:Halting rx",dev->name);
					}

				}

			}
			fda_ent_ptr++;
		}
		spin_unlock(&priv->rx_lock);

		/* Clear the  interrupts */
		writel(ETHER_INT_SRC_INTMACRX_MSK | ETHER_INT_SRC_FDAEX_MSK
		       | ETHER_INT_SRC_BLEX_MSK,ETHER_INT_SRC(dev->base_addr));

	}

	if(readl(ETHER_INT_SRC(dev->base_addr))&ETHER_INT_SRC_INTMACTX_MSK){

		if(!priv->memupdate_scheduled){
			schedule_task(&priv->tq_memupdate);
			priv->memupdate_scheduled=1;
		}
		/* Clear the interrupt */
		writel(ETHER_INT_SRC_INTMACTX_MSK,ETHER_INT_SRC(dev->base_addr));
	}

	if (readl(ETHER_INT_SRC(dev->base_addr)) & (ETHER_INT_SRC_SWINT_MSK|
						    ETHER_INT_SRC_INTEARNOT_MSK|
						    ETHER_INT_SRC_INTLINK_MSK|
						    ETHER_INT_SRC_INTEXBD_MSK|
						    ETHER_INT_SRC_INTTXCTLCMP_MSK))
	{
		/*
		 *	Not using any of these so they shouldn't happen
		 *
		 *	In the cased of INTEXBD - if you allocate more
		 *      than 28 decsriptors you may need to think about this
		 */
		printk("Not using this interrupt\n");
	}

	if (readl(ETHER_INT_SRC(dev->base_addr)) &
	    (ETHER_INT_SRC_INTSBUS_MSK |
	     ETHER_INT_SRC_INTNRABT_MSK
	     |ETHER_INT_SRC_DMPARERR_MSK))
	{
		/*
		 * Hardware errors, we can either ignore them and hope they go away
		 *or reset the device, I'll try the first for now to see if they happen
		 */
		printk("Hardware error\n");
	}
}

static void ether00_setup_ethernet_address(struct net_device* dev)
{
	int tmp;

	dev->addr_len=6;
	writew(0,ETHER_ARC_ADR(dev->base_addr));
	writel((dev->dev_addr[0]<<24) |
		(dev->dev_addr[1]<<16) |
		(dev->dev_addr[2]<<8) |
		dev->dev_addr[3],
		ETHER_ARC_DATA(dev->base_addr));

	writew(4,ETHER_ARC_ADR(dev->base_addr));
	tmp=readl(ETHER_ARC_DATA(dev->base_addr));
	tmp&=0xffff;
	tmp|=(dev->dev_addr[4]<<24) | (dev->dev_addr[5]<<16);
	writel(tmp, ETHER_ARC_DATA(dev->base_addr));
	/* Enable this entry in the ARC */

	writel(1,ETHER_ARC_ENA(dev->base_addr));

	return;
}


static void ether00_reset(struct net_device *dev)
{
	/* reset the controller */
	writew(ETHER_MAC_CTL_RESET_MSK,ETHER_MAC_CTL(dev->base_addr));

	/*
	 * Make sure we're not going to send anything
	 */

	writew(ETHER_TX_CTL_TXHALT_MSK,ETHER_TX_CTL(dev->base_addr));

	/*
	 * Make sure we're not going to receive anything
	 */
	writew(ETHER_RX_CTL_RXHALT_MSK,ETHER_RX_CTL(dev->base_addr));

	/*
	 * Disable Interrupts for now, and set the burst size to 8 bytes
	 */

	writel(ETHER_DMA_CTL_INTMASK_MSK |
	       ((8 << ETHER_DMA_CTL_DMBURST_OFST) & ETHER_DMA_CTL_DMBURST_MSK)
	       |(2<<ETHER_DMA_CTL_RXALIGN_OFST),
	       ETHER_DMA_CTL(dev->base_addr));


	/*
	 * Set TxThrsh - start transmitting a packet after 1514
	 * bytes or when a packet is complete, whichever comes first
	 */
	 writew(1514,ETHER_TXTHRSH(dev->base_addr));

	/*
	 * Set TxPollCtr.  Each cycle is
	 * 61.44 microseconds with a 33 MHz bus
	 */
	 writew(1,ETHER_TXPOLLCTR(dev->base_addr));

	/*
	 * Set Rx_Ctl - Turn off reception and let RxData turn it
	 * on later
	 */
	 writew(ETHER_RX_CTL_RXHALT_MSK,ETHER_RX_CTL(dev->base_addr));

}


static void ether00_set_multicast(struct net_device* dev)
{
	int count=dev->mc_count;

	/* Set promiscuous mode if it's asked for. */

	if (dev->flags&IFF_PROMISC){

		writew( ETHER_ARC_CTL_COMPEN_MSK |
			ETHER_ARC_CTL_BROADACC_MSK |
			ETHER_ARC_CTL_GROUPACC_MSK |
			ETHER_ARC_CTL_STATIONACC_MSK,
			ETHER_ARC_CTL(dev->base_addr));
		return;
	}

	/*
	 * Get all multicast packets if required, or if there are too
	 * many addresses to fit in hardware
	 */
	if (dev->flags & IFF_ALLMULTI){
		writew( ETHER_ARC_CTL_COMPEN_MSK |
			ETHER_ARC_CTL_GROUPACC_MSK |
			ETHER_ARC_CTL_BROADACC_MSK,
			ETHER_ARC_CTL(dev->base_addr));
		return;
	}
	if (dev->mc_count > (ETHER_ARC_SIZE - 1)){

		printk(KERN_WARNING "Too many multicast addresses for hardware to filter - receiving all multicast packets\n");
		writew( ETHER_ARC_CTL_COMPEN_MSK |
			ETHER_ARC_CTL_GROUPACC_MSK |
			ETHER_ARC_CTL_BROADACC_MSK,
			ETHER_ARC_CTL(dev->base_addr));
		return;
	}

	if(dev->mc_count){
		struct dev_mc_list *mc_list_ent=dev->mc_list;
		unsigned int temp,i;
		DEBUG(printk("mc_count=%d mc_list=%#x\n",dev-> mc_count, dev->mc_list));
		DEBUG(printk("mc addr=%02#x%02x%02x%02x%02x%02x\n",
			     mc_list_ent->dmi_addr[5],
			     mc_list_ent->dmi_addr[4],
			     mc_list_ent->dmi_addr[3],
			     mc_list_ent->dmi_addr[2],
			     mc_list_ent->dmi_addr[1],
			     mc_list_ent->dmi_addr[0]);)

		/*
		 * The first 6 bytes are the MAC address, so
		 * don't change them!
		 */
		writew(4,ETHER_ARC_ADR(dev->base_addr));
		temp=readl(ETHER_ARC_DATA(dev->base_addr));
		temp&=0xffff0000;

		/* Disable the current multicast stuff */
		writel(1,ETHER_ARC_ENA(dev->base_addr));

		for(;;){
			temp|=mc_list_ent->dmi_addr[1] |
				mc_list_ent->dmi_addr[0]<<8;
			writel(temp,ETHER_ARC_DATA(dev->base_addr));

			i=readl(ETHER_ARC_ADR(dev->base_addr));
			writew(i+4,ETHER_ARC_ADR(dev->base_addr));

			temp=mc_list_ent->dmi_addr[5]|
				mc_list_ent->dmi_addr[4]<<8 |
				mc_list_ent->dmi_addr[3]<<16 |
				mc_list_ent->dmi_addr[2]<<24;
			writel(temp,ETHER_ARC_DATA(dev->base_addr));

			count--;
			if(!mc_list_ent->next || !count){
				break;
			}
			DEBUG(printk("mc_list_next=%#x\n",mc_list_ent->next);)
			mc_list_ent=mc_list_ent->next;


			i=readl(ETHER_ARC_ADR(dev->base_addr));
			writel(i+4,ETHER_ARC_ADR(dev->base_addr));

			temp=mc_list_ent->dmi_addr[3]|
				mc_list_ent->dmi_addr[2]<<8 |
				mc_list_ent->dmi_addr[1]<<16 |
				mc_list_ent->dmi_addr[0]<<24;
			writel(temp,ETHER_ARC_DATA(dev->base_addr));

			i=readl(ETHER_ARC_ADR(dev->base_addr));
			writel(i+4,ETHER_ARC_ADR(dev->base_addr));

			temp=mc_list_ent->dmi_addr[4]<<16 |
				mc_list_ent->dmi_addr[5]<<24;

			writel(temp,ETHER_ARC_DATA(dev->base_addr));

			count--;
			if(!mc_list_ent->next || !count){
				break;
			}
			mc_list_ent=mc_list_ent->next;
		}


		if(count)
			printk(KERN_WARNING "Multicast list size error\n");


		writew( ETHER_ARC_CTL_BROADACC_MSK|
			ETHER_ARC_CTL_COMPEN_MSK,
			ETHER_ARC_CTL(dev->base_addr));

	}

	/* enable the active ARC enties */
	writew((1<<(count+2))-1,ETHER_ARC_ENA(dev->base_addr));
}


static int ether00_open(struct net_device* dev)
{
	int result,tmp;
	struct net_priv* priv;

	if (!is_valid_ether_addr(dev->dev_addr))
		return -EINVAL;

	/* Install interrupt handlers */
	result=request_irq(dev->irq,ether00_int,0,"ether00",dev);
	if(result)
		goto open_err1;

	result=request_irq(2,ether00_phy_int,0,"ether00_phy",dev);
	if(result)
		goto open_err2;

	ether00_reset(dev);
	result=ether00_mem_init(dev);
	if(result)
		goto open_err3;


	ether00_setup_ethernet_address(dev);

	ether00_set_multicast(dev);

	result=ether00_write_phy(dev,PHY_CONTROL, PHY_CONTROL_ANEGEN_MSK | PHY_CONTROL_RANEG_MSK);
	if(result)
		goto open_err4;
	result=ether00_write_phy(dev,PHY_IRQ_CONTROL, PHY_IRQ_CONTROL_LS_CHG_IE_MSK |
				 PHY_IRQ_CONTROL_ANEG_COMP_IE_MSK);
	if(result)
		goto open_err4;

	/* Start the device enable interrupts */
	writew(ETHER_RX_CTL_RXEN_MSK
//	       | ETHER_RX_CTL_STRIPCRC_MSK
	       | ETHER_RX_CTL_ENGOOD_MSK
	       | ETHER_RX_CTL_ENRXPAR_MSK| ETHER_RX_CTL_ENLONGERR_MSK
	       | ETHER_RX_CTL_ENOVER_MSK| ETHER_RX_CTL_ENCRCERR_MSK,
	       ETHER_RX_CTL(dev->base_addr));

	writew(ETHER_TX_CTL_TXEN_MSK|
	       ETHER_TX_CTL_ENEXDEFER_MSK|
	       ETHER_TX_CTL_ENLCARR_MSK|
	       ETHER_TX_CTL_ENEXCOLL_MSK|
	       ETHER_TX_CTL_ENLATECOLL_MSK|
	       ETHER_TX_CTL_ENTXPAR_MSK|
	       ETHER_TX_CTL_ENCOMP_MSK,
	       ETHER_TX_CTL(dev->base_addr));

	tmp=readl(ETHER_DMA_CTL(dev->base_addr));
	writel(tmp&~ETHER_DMA_CTL_INTMASK_MSK,ETHER_DMA_CTL(dev->base_addr));

	return 0;

 open_err4:
	ether00_reset(dev);
 open_err3:
	free_irq(2,dev);
 open_err2:
	free_irq(dev->irq,dev);
 open_err1:
	return result;

}


static int ether00_tx(struct sk_buff* skb, struct net_device* dev)
{
	struct net_priv *priv=dev->priv;
	struct tx_fda_ent *fda_ptr;
	int i;


	/*
	 *	Find an empty slot in which to stick the frame
	 */
	fda_ptr=(struct tx_fda_ent*)__dma_va(readl(ETHER_TXFRMPTR(dev->base_addr)));
	i=0;
	while(i<TX_NUM_FDESC){
		if (fda_ptr->fd.FDStat||(fda_ptr->fd.FDCtl & FDCTL_COWNSFD_MSK)){
			fda_ptr =(struct tx_fda_ent*) __dma_va((struct tx_fda_ent*)fda_ptr->fd.FDNext);
		}
		else {
			break;
		}
		i++;
	}

	/* Write the skb data from the cache*/
	consistent_sync(skb->data,skb->len,PCI_DMA_TODEVICE);
	fda_ptr->bd.BuffData=(char*)__pa(skb->data);
	fda_ptr->bd.BuffLength=(unsigned short)skb->len;
	/* Save the pointer to the skb for freeing later */
	fda_ptr->fd.FDSystem=(unsigned int)skb;
	fda_ptr->fd.FDStat=0;
	/* Pass ownership of the buffers to the controller */
	fda_ptr->fd.FDCtl=1;
	fda_ptr->fd.FDCtl|=FDCTL_COWNSFD_MSK;

	/* If the next buffer in the list is full, stop the queue */
	fda_ptr=(struct tx_fda_ent*)__dma_va(fda_ptr->fd.FDNext);
	if ((fda_ptr->fd.FDStat)||(fda_ptr->fd.FDCtl & FDCTL_COWNSFD_MSK)){
		netif_stop_queue(dev);
		priv->queue_stopped=1;
	}

	return 0;
}

static struct net_device_stats *ether00_stats(struct net_device* dev)
{
	struct net_priv *priv=dev->priv;
	return &priv->stats;
}


static int ether00_stop(struct net_device* dev)
{
	struct net_priv *priv=dev->priv;
	int tmp;

	/* Stop/disable the device. */
	tmp=readw(ETHER_RX_CTL(dev->base_addr));
	tmp&=~(ETHER_RX_CTL_RXEN_MSK | ETHER_RX_CTL_ENGOOD_MSK);
	tmp|=ETHER_RX_CTL_RXHALT_MSK;
	writew(tmp,ETHER_RX_CTL(dev->base_addr));

	tmp=readl(ETHER_TX_CTL(dev->base_addr));
	tmp&=~ETHER_TX_CTL_TXEN_MSK;
	tmp|=ETHER_TX_CTL_TXHALT_MSK;
	writel(tmp,ETHER_TX_CTL(dev->base_addr));

	/* Free up system resources */
	free_irq(dev->irq,dev);
	free_irq(2,dev);
	iounmap(priv->dma_data);

	return 0;
}


static void ether00_get_ethernet_address(struct net_device* dev)
{
	struct mtd_info *mymtd=NULL;
	int i;
	size_t retlen;

	/*
	 * For the Epxa10 dev board (camelot), the ethernet MAC
	 * address is of the form  00:aa:aa:00:xx:xx where
	 * 00:aa:aa is the Altera vendor ID and xx:xx is the
	 * last 2 bytes of the board serial number, as programmed
	 * into the OTP area of the flash device on EBI1. If this
	 * isn't an expa10 dev board, or there's no mtd support to
	 * read the serial number from flash then we'll force the
	 * use to set their own mac address using ifconfig.
	 */

#ifdef CONFIG_ARCH_CAMELOT
#ifdef CONFIG_MTD
	/* get the mtd_info structure for the first mtd device*/
	for(i=0;i<MAX_MTD_DEVICES;i++){
		mymtd=get_mtd_device(NULL,i);
		if(!mymtd||!strcmp(mymtd->name,"EPXA10DB flash"))
			break;
	}

	if(!mymtd || !mymtd->read_user_prot_reg){
		printk(KERN_WARNING "%s: Failed to read MAC address from flash\n",dev->name);
	}else{
		mymtd->read_user_prot_reg(mymtd,2,1,&retlen,&dev->dev_addr[5]);
		mymtd->read_user_prot_reg(mymtd,3,1,&retlen,&dev->dev_addr[4]);
		dev->dev_addr[3]=0;
		dev->dev_addr[2]=vendor_id[1];
		dev->dev_addr[1]=vendor_id[0];
		dev->dev_addr[0]=0;
	}
#else
	printk(KERN_WARNING "%s: MTD support required to read MAC address from EPXA10 dev board\n", dev->name);
#endif
#endif

	if (!is_valid_ether_addr(dev->dev_addr))
		printk("%s: Invalid ethernet MAC address.  Please set using "
			"ifconfig\n", dev->name);

}

/*
 * Keep a mapping of dev_info addresses -> port lines to use when
 * removing ports dev==NULL indicates unused entry
 */


static struct net_device* dev_list[ETH_NR];

static int ether00_add_device(struct pldhs_dev_info* dev_info,void* dev_ps_data)
{
	struct net_device *dev;
	struct net_priv *priv;
	void *map_addr;
	int result;
	int i;

	i=0;
	while(dev_list[i] && i < ETH_NR)
		i++;

	if(i==ETH_NR){
		printk(KERN_WARNING "ether00: Maximum number of ports reached\n");
		return 0;
	}


	if (!request_mem_region(dev_info->base_addr, MAC_REG_SIZE, "ether00"))
		return -EBUSY;

	dev = alloc_etherdev(sizeof(struct net_priv));
	if(!dev) {
		result = -ENOMEM;
		goto out_release;
	}
	priv = dev->priv;

	priv->tq_memupdate.routine=ether00_mem_update;
	priv->tq_memupdate.data=(void*) dev;

	spin_lock_init(&priv->rx_lock);

	map_addr=ioremap_nocache(dev_info->base_addr,SZ_4K);
	if(!map_addr){
		result = -ENOMEM;
		out_kfree;
	}

	dev->open=ether00_open;
	dev->stop=ether00_stop;
	dev->set_multicast_list=ether00_set_multicast;
	dev->hard_start_xmit=ether00_tx;
	dev->get_stats=ether00_stats;

	ether00_get_ethernet_address(dev);

	SET_MODULE_OWNER(dev);

	dev->base_addr=(unsigned int)map_addr;
	dev->irq=dev_info->irq;
	dev->features=NETIF_F_DYNALLOC | NETIF_F_HW_CSUM;

	result=register_netdev(dev);
	if(result){
		printk("Ether00: Error %i registering driver\n",result);
		goto out_unmap;
	}
	printk("registered ether00 device at %#x\n",dev_info->base_addr);

	dev_list[i]=dev;

	return result;

 out_unmap:
	iounmap(map_addr);
 out_kfree:
	free_netdev(dev);
 out_release:
	release_mem_region(dev_info->base_addr, MAC_REG_SIZE);
	return result;
}


static int ether00_remove_devices(void)
{
	int i;

	for(i=0;i<ETH_NR;i++){
		if(dev_list[i]){
			netif_device_detach(dev_list[i]);
			unregister_netdev(dev_list[i]);
			iounmap((void*)dev_list[i]->base_addr);
			release_mem_region(dev_list[i]->base_addr, MAC_REG_SIZE);
			free_netdev(dev_list[i]);
			dev_list[i]=0;
		}
	}
	return 0;
}

static struct pld_hotswap_ops ether00_pldhs_ops={
	.name = ETHER00_NAME,
	.add_device = ether00_add_device,
	.remove_devices = ether00_remove_devices,
};


static void __exit ether00_cleanup_module(void)
{
	int result;
	result=ether00_remove_devices();
	if(result)
		printk(KERN_WARNING "ether00: failed to remove all devices\n");

	pldhs_unregister_driver(ETHER00_NAME);
}
module_exit(ether00_cleanup_module);


static int __init ether00_mod_init(void)
{
	printk("mod init\n");
	return pldhs_register_driver(&ether00_pldhs_ops);

}

module_init(ether00_mod_init);

