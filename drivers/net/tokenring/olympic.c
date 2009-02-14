/*
 *   olympic.c (c) 1999 Peter De Schrijver All Rights Reserved
 *		   1999/2000 Mike Phillips (mikep@linuxtr.net)
 *
 *  Linux driver for IBM PCI tokenring cards based on the Pit/Pit-Phy/Olympic
 *  chipset. 
 *
 *  Base Driver Skeleton:
 *      Written 1993-94 by Donald Becker.
 *
 *      Copyright 1993 United States Government as represented by the
 *      Director, National Security Agency.
 *
 *  Thanks to Erik De Cock, Adrian Bridgett and Frank Fiene for their 
 *  assistance and perserverance with the testing of this driver.
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License, incorporated herein by reference.
 * 
 *  4/27/99 - Alpha Release 0.1.0
 *            First release to the public
 *
 *  6/8/99  - Official Release 0.2.0   
 *            Merged into the kernel code 
 *  8/18/99 - Updated driver for 2.3.13 kernel to use new pci
 *	      resource. Driver also reports the card name returned by
 *            the pci resource.
 *  1/11/00 - Added spinlocks for smp
 *  2/23/00 - Updated to dev_kfree_irq 
 *  3/10/00 - Fixed FDX enable which triggered other bugs also 
 *            squashed.
 *  5/20/00 - Changes to handle Olympic on LinuxPPC. Endian changes.
 *            The odd thing about the changes is that the fix for
 *            endian issues with the big-endian data in the arb, asb...
 *            was to always swab() the bytes, no matter what CPU.
 *            That's because the read[wl]() functions always swap the
 *            bytes on the way in on PPC.
 *            Fixing the hardware descriptors was another matter,
 *            because they weren't going through read[wl](), there all
 *            the results had to be in memory in le32 values. kdaaker
 *
 * 12/23/00 - Added minimal Cardbus support (Thanks Donald).
 *
 * 03/09/01 - Add new pci api, dev_base_lock, general clean up. 
 *
 * 03/27/01 - Add new dma pci (Thanks to Kyle Lucke) and alloc_trdev
 *	      Change proc_fs behaviour, now one entry per adapter.
 *
 * 04/09/01 - Couple of bug fixes to the dma unmaps and ejecting the
 *	      adapter when live does not take the system down with it.
 * 
 * 06/02/01 - Clean up, copy skb for small packets
 * 
 * 06/22/01 - Add EISR error handling routines 
 *
 * 07/19/01 - Improve bad LAA reporting, strip out freemem
 *	      into a separate function, its called from 3 
 *	      different places now. 
 * 02/09/02 - Replaced sleep_on. 
 * 03/01/02 - Replace access to several registers from 32 bit to 
 * 	      16 bit. Fixes alignment errors on PPC 64 bit machines.
 * 	      Thanks to Al Trautman for this one.
 * 03/10/02 - Fix BUG in arb_cmd. Bug was there all along but was
 * 	      silently ignored until the error checking code 
 * 	      went into version 1.0.0 
 * 06/04/02 - Add correct start up sequence for the cardbus adapters.
 * 	      Required for strict compliance with pci power mgmt specs.
 *  To Do:
 *
 *	     Wake on lan	
 * 
 *  If Problems do Occur
 *  Most problems can be rectified by either closing and opening the interface
 *  (ifconfig down and up) or rmmod and insmod'ing the driver (a bit difficult
 *  if compiled into the kernel).
 */

/* Change OLYMPIC_DEBUG to 1 to get verbose, and I mean really verbose, messages */

#define OLYMPIC_DEBUG 0


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#include <net/checksum.h>
#include <net/net_namespace.h>

#include <asm/io.h>
#include <asm/system.h>

#include "olympic.h"

/* I've got to put some intelligence into the version number so that Peter and I know
 * which version of the code somebody has got. 
 * Version Number = a.b.c.d  where a.b.c is the level of code and d is the latest author.
 * So 0.0.1.pds = Peter, 0.0.1.mlp = Mike
 * 
 * Official releases will only have an a.b.c version number format. 
 */

static char version[] =
"Olympic.c v1.0.5 6/04/02 - Peter De Schrijver & Mike Phillips" ; 

static char *open_maj_error[]  = {"No error", "Lobe Media Test", "Physical Insertion",
				   "Address Verification", "Neighbor Notification (Ring Poll)",
				   "Request Parameters","FDX Registration Request",
				   "FDX Duplicate Address Check", "Station registration Query Wait",
				   "Unknown stage"};

static char *open_min_error[] = {"No error", "Function Failure", "Signal Lost", "Wire Fault",
				   "Ring Speed Mismatch", "Timeout","Ring Failure","Ring Beaconing",
				   "Duplicate Node Address","Request Parameters","Remove Received",
				   "Reserved", "Reserved", "No Monitor Detected for RPL", 
				   "Monitor Contention failer for RPL", "FDX Protocol Error"};

/* Module paramters */

MODULE_AUTHOR("Mike Phillips <mikep@linuxtr.net>") ; 
MODULE_DESCRIPTION("Olympic PCI/Cardbus Chipset Driver") ; 

/* Ring Speed 0,4,16,100 
 * 0 = Autosense         
 * 4,16 = Selected speed only, no autosense
 * This allows the card to be the first on the ring
 * and become the active monitor.
 * 100 = Nothing at present, 100mbps is autodetected
 * if FDX is turned on. May be implemented in the future to 
 * fail if 100mpbs is not detected.
 *
 * WARNING: Some hubs will allow you to insert
 * at the wrong speed
 */

static int ringspeed[OLYMPIC_MAX_ADAPTERS] = {0,} ;
module_param_array(ringspeed, int, NULL, 0);

/* Packet buffer size */

static int pkt_buf_sz[OLYMPIC_MAX_ADAPTERS] = {0,} ;
module_param_array(pkt_buf_sz, int, NULL, 0) ;

/* Message Level */

static int message_level[OLYMPIC_MAX_ADAPTERS] = {0,} ; 
module_param_array(message_level, int, NULL, 0) ;

/* Change network_monitor to receive mac frames through the arb channel.
 * Will also create a /proc/net/olympic_tr%d entry, where %d is the tr
 * device, i.e. tr0, tr1 etc. 
 * Intended to be used to create a ring-error reporting network module 
 * i.e. it will give you the source address of beaconers on the ring 
 */
static int network_monitor[OLYMPIC_MAX_ADAPTERS] = {0,};
module_param_array(network_monitor, int, NULL, 0);

static struct pci_device_id olympic_pci_tbl[] = {
	{PCI_VENDOR_ID_IBM,PCI_DEVICE_ID_IBM_TR_WAKE,PCI_ANY_ID,PCI_ANY_ID,},
	{ } 	/* Terminating Entry */
};
MODULE_DEVICE_TABLE(pci,olympic_pci_tbl) ; 


static int olympic_probe(struct pci_dev *pdev, const struct pci_device_id *ent); 
static int olympic_init(struct net_device *dev);
static int olympic_open(struct net_device *dev);
static int olympic_xmit(struct sk_buff *skb, struct net_device *dev);
static int olympic_close(struct net_device *dev);
static void olympic_set_rx_mode(struct net_device *dev);
static void olympic_freemem(struct net_device *dev) ;  
static irqreturn_t olympic_interrupt(int irq, void *dev_id);
static int olympic_set_mac_address(struct net_device *dev, void *addr) ; 
static void olympic_arb_cmd(struct net_device *dev);
static int olympic_change_mtu(struct net_device *dev, int mtu);
static void olympic_srb_bh(struct net_device *dev) ; 
static void olympic_asb_bh(struct net_device *dev) ; 
static int olympic_proc_info(char *buffer, char **start, off_t offset, int length, int *eof, void *data) ; 

static const struct net_device_ops olympic_netdev_ops = {
	.ndo_open		= olympic_open,
	.ndo_stop		= olympic_close,
	.ndo_start_xmit		= olympic_xmit,
	.ndo_change_mtu		= olympic_change_mtu,
	.ndo_set_multicast_list	= olympic_set_rx_mode,
	.ndo_set_mac_address	= olympic_set_mac_address,
};

static int __devinit olympic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev ; 
	struct olympic_private *olympic_priv;
	static int card_no = -1 ;
	int i ; 

	card_no++ ; 

	if ((i = pci_enable_device(pdev))) {
		return i ; 
	}

	pci_set_master(pdev);

	if ((i = pci_request_regions(pdev,"olympic"))) { 
		goto op_disable_dev;
	}
 
	dev = alloc_trdev(sizeof(struct olympic_private)) ; 
	if (!dev) {
		i = -ENOMEM; 
		goto op_release_dev;
	}

	olympic_priv = netdev_priv(dev) ;
	
	spin_lock_init(&olympic_priv->olympic_lock) ; 

	init_waitqueue_head(&olympic_priv->srb_wait);
	init_waitqueue_head(&olympic_priv->trb_wait);
#if OLYMPIC_DEBUG  
	printk(KERN_INFO "pci_device: %p, dev:%p, dev->priv: %p\n", pdev, dev, netdev_priv(dev));
#endif
	dev->irq=pdev->irq;
	dev->base_addr=pci_resource_start(pdev, 0);
	olympic_priv->olympic_card_name = pci_name(pdev);
	olympic_priv->pdev = pdev; 
	olympic_priv->olympic_mmio = ioremap(pci_resource_start(pdev,1),256);
	olympic_priv->olympic_lap = ioremap(pci_resource_start(pdev,2),2048);
	if (!olympic_priv->olympic_mmio || !olympic_priv->olympic_lap) {
		goto op_free_iomap;
	}
				
	if ((pkt_buf_sz[card_no] < 100) || (pkt_buf_sz[card_no] > 18000) )
		olympic_priv->pkt_buf_sz = PKT_BUF_SZ ; 
	else
		olympic_priv->pkt_buf_sz = pkt_buf_sz[card_no] ; 

	dev->mtu = olympic_priv->pkt_buf_sz - TR_HLEN ; 
	olympic_priv->olympic_ring_speed = ringspeed[card_no] ; 
	olympic_priv->olympic_message_level = message_level[card_no] ; 
	olympic_priv->olympic_network_monitor = network_monitor[card_no];
	
	if ((i = olympic_init(dev))) {
		goto op_free_iomap;
	}				

	dev->netdev_ops = &olympic_netdev_ops;
	SET_NETDEV_DEV(dev, &pdev->dev);

	pci_set_drvdata(pdev,dev) ; 
	register_netdev(dev) ; 
	printk("Olympic: %s registered as: %s\n",olympic_priv->olympic_card_name,dev->name);
	if (olympic_priv->olympic_network_monitor) { /* Must go after register_netdev as we need the device name */ 
		char proc_name[20] ; 
		strcpy(proc_name,"olympic_") ;
		strcat(proc_name,dev->name) ; 
		create_proc_read_entry(proc_name,0,init_net.proc_net,olympic_proc_info,(void *)dev) ;
		printk("Olympic: Network Monitor information: /proc/%s\n",proc_name); 
	}
	return  0 ;

op_free_iomap:
	if (olympic_priv->olympic_mmio)
		iounmap(olympic_priv->olympic_mmio); 
	if (olympic_priv->olympic_lap)
		iounmap(olympic_priv->olympic_lap);

	free_netdev(dev);
op_release_dev:
	pci_release_regions(pdev); 

op_disable_dev:
	pci_disable_device(pdev);
	return i;
}

static int olympic_init(struct net_device *dev)
{
    	struct olympic_private *olympic_priv;
	u8 __iomem *olympic_mmio, *init_srb,*adapter_addr;
	unsigned long t; 
	unsigned int uaa_addr;

	olympic_priv=netdev_priv(dev);
	olympic_mmio=olympic_priv->olympic_mmio;

	printk("%s \n", version);
	printk("%s. I/O at %hx, MMIO at %p, LAP at %p, using irq %d\n", olympic_priv->olympic_card_name, (unsigned int) dev->base_addr,olympic_priv->olympic_mmio, olympic_priv->olympic_lap, dev->irq);

	writel(readl(olympic_mmio+BCTL) | BCTL_SOFTRESET,olympic_mmio+BCTL);
	t=jiffies;
	while((readl(olympic_mmio+BCTL)) & BCTL_SOFTRESET) {
		schedule();		
		if(time_after(jiffies, t + 40*HZ)) {
			printk(KERN_ERR "IBM PCI tokenring card not responding.\n");
			return -ENODEV;
		}
	}


	/* Needed for cardbus */
	if(!(readl(olympic_mmio+BCTL) & BCTL_MODE_INDICATOR)) {
		writel(readl(olympic_priv->olympic_mmio+FERMASK)|FERMASK_INT_BIT, olympic_mmio+FERMASK);
	}
	
#if OLYMPIC_DEBUG
	printk("BCTL: %x\n",readl(olympic_mmio+BCTL));
	printk("GPR: %x\n",readw(olympic_mmio+GPR));
	printk("SISRMASK: %x\n",readl(olympic_mmio+SISR_MASK));
#endif
	/* Aaaahhh, You have got to be real careful setting GPR, the card
	   holds the previous values from flash memory, including autosense 
           and ring speed */

	writel(readl(olympic_mmio+BCTL)|BCTL_MIMREB,olympic_mmio+BCTL);
	
	if (olympic_priv->olympic_ring_speed  == 0) { /* Autosense */
		writew(readw(olympic_mmio+GPR)|GPR_AUTOSENSE,olympic_mmio+GPR);
		if (olympic_priv->olympic_message_level) 
			printk(KERN_INFO "%s: Ringspeed autosense mode on\n",olympic_priv->olympic_card_name);
	} else if (olympic_priv->olympic_ring_speed == 16) {
		if (olympic_priv->olympic_message_level) 
			printk(KERN_INFO "%s: Trying to open at 16 Mbps as requested\n", olympic_priv->olympic_card_name);
		writew(GPR_16MBPS, olympic_mmio+GPR);
	} else if (olympic_priv->olympic_ring_speed == 4) {
		if (olympic_priv->olympic_message_level) 
			printk(KERN_INFO "%s: Trying to open at 4 Mbps as requested\n", olympic_priv->olympic_card_name) ; 
		writew(0, olympic_mmio+GPR);
	} 
	
	writew(readw(olympic_mmio+GPR)|GPR_NEPTUNE_BF,olympic_mmio+GPR);

#if OLYMPIC_DEBUG
	printk("GPR = %x\n",readw(olympic_mmio + GPR) ) ; 
#endif
	/* Solo has been paused to meet the Cardbus power
	 * specs if the adapter is cardbus. Check to 
	 * see its been paused and then restart solo. The
	 * adapter should set the pause bit within 1 second.
	 */

	if(!(readl(olympic_mmio+BCTL) & BCTL_MODE_INDICATOR)) { 
		t=jiffies;
		while (!(readl(olympic_mmio+CLKCTL) & CLKCTL_PAUSE)) {
			schedule() ; 
			if(time_after(jiffies, t + 2*HZ)) {
				printk(KERN_ERR "IBM Cardbus tokenring adapter not responsing.\n") ; 
				return -ENODEV;
			}
		}
		writel(readl(olympic_mmio+CLKCTL) & ~CLKCTL_PAUSE, olympic_mmio+CLKCTL) ; 
	}
	
	/* start solo init */
	writel((1<<15),olympic_mmio+SISR_MASK_SUM);

	t=jiffies;
	while(!((readl(olympic_mmio+SISR_RR)) & SISR_SRB_REPLY)) {
		schedule();		
		if(time_after(jiffies, t + 15*HZ)) {
			printk(KERN_ERR "IBM PCI tokenring card not responding.\n");
			return -ENODEV;
		}
	}
	
	writel(readw(olympic_mmio+LAPWWO),olympic_mmio+LAPA);

#if OLYMPIC_DEBUG
	printk("LAPWWO: %x, LAPA: %x\n",readl(olympic_mmio+LAPWWO), readl(olympic_mmio+LAPA));
#endif

	init_srb=olympic_priv->olympic_lap + ((readw(olympic_mmio+LAPWWO)) & (~0xf800));

#if OLYMPIC_DEBUG		
{
	int i;
	printk("init_srb(%p): ",init_srb);
	for(i=0;i<20;i++)
		printk("%x ",readb(init_srb+i));
	printk("\n");
}
#endif	
	if(readw(init_srb+6)) {
		printk(KERN_INFO "tokenring card initialization failed. errorcode : %x\n",readw(init_srb+6));
		return -ENODEV;
	}

	if (olympic_priv->olympic_message_level) {
		if ( readb(init_srb +2) & 0x40) { 
			printk(KERN_INFO "Olympic: Adapter is FDX capable.\n") ;
		} else { 
			printk(KERN_INFO "Olympic: Adapter cannot do FDX.\n");
		}
	}
  
	uaa_addr=swab16(readw(init_srb+8));

#if OLYMPIC_DEBUG
	printk("UAA resides at %x\n",uaa_addr);
#endif

	writel(uaa_addr,olympic_mmio+LAPA);
	adapter_addr=olympic_priv->olympic_lap + (uaa_addr & (~0xf800));

	memcpy_fromio(&dev->dev_addr[0], adapter_addr,6);

#if OLYMPIC_DEBUG
	printk("adapter address: %pM\n", dev->dev_addr);
#endif

	olympic_priv->olympic_addr_table_addr = swab16(readw(init_srb + 12)); 
	olympic_priv->olympic_parms_addr = swab16(readw(init_srb + 14)); 

	return 0;

}

static int olympic_open(struct net_device *dev)
{
	struct olympic_private *olympic_priv=netdev_priv(dev);
	u8 __iomem *olympic_mmio=olympic_priv->olympic_mmio,*init_srb;
	unsigned long flags, t;
	int i, open_finished = 1 ;
	u8 resp, err;

	DECLARE_WAITQUEUE(wait,current) ; 

	olympic_init(dev);

	if(request_irq(dev->irq, &olympic_interrupt, IRQF_SHARED , "olympic", dev)) {
		return -EAGAIN;
	}

#if OLYMPIC_DEBUG
	printk("BMCTL: %x\n",readl(olympic_mmio+BMCTL_SUM));
	printk("pending ints: %x\n",readl(olympic_mmio+SISR_RR));
#endif

	writel(SISR_MI,olympic_mmio+SISR_MASK_SUM);

	writel(SISR_MI | SISR_SRB_REPLY, olympic_mmio+SISR_MASK); /* more ints later, doesn't stop arb cmd interrupt */

	writel(LISR_LIE,olympic_mmio+LISR); /* more ints later */

	/* adapter is closed, so SRB is pointed to by LAPWWO */

	writel(readw(olympic_mmio+LAPWWO),olympic_mmio+LAPA);
	init_srb=olympic_priv->olympic_lap + ((readw(olympic_mmio+LAPWWO)) & (~0xf800));
	
#if OLYMPIC_DEBUG
	printk("LAPWWO: %x, LAPA: %x\n",readw(olympic_mmio+LAPWWO), readl(olympic_mmio+LAPA));
	printk("SISR Mask = %04x\n", readl(olympic_mmio+SISR_MASK));
	printk("Before the open command \n");
#endif	
	do {
		memset_io(init_srb,0,SRB_COMMAND_SIZE);

		writeb(SRB_OPEN_ADAPTER,init_srb) ; 	/* open */
		writeb(OLYMPIC_CLEAR_RET_CODE,init_srb+2);

		/* If Network Monitor, instruct card to copy MAC frames through the ARB */
		if (olympic_priv->olympic_network_monitor) 
			writew(swab16(OPEN_ADAPTER_ENABLE_FDX | OPEN_ADAPTER_PASS_ADC_MAC | OPEN_ADAPTER_PASS_ATT_MAC | OPEN_ADAPTER_PASS_BEACON), init_srb+8);
		else
			writew(swab16(OPEN_ADAPTER_ENABLE_FDX), init_srb+8);
	
		/* Test OR of first 3 bytes as its totally possible for 
		 * someone to set the first 2 bytes to be zero, although this 
		 * is an error, the first byte must have bit 6 set to 1  */

		if (olympic_priv->olympic_laa[0] | olympic_priv->olympic_laa[1] | olympic_priv->olympic_laa[2]) {
			writeb(olympic_priv->olympic_laa[0],init_srb+12);
			writeb(olympic_priv->olympic_laa[1],init_srb+13);
			writeb(olympic_priv->olympic_laa[2],init_srb+14);
			writeb(olympic_priv->olympic_laa[3],init_srb+15);
			writeb(olympic_priv->olympic_laa[4],init_srb+16);
			writeb(olympic_priv->olympic_laa[5],init_srb+17);
			memcpy(dev->dev_addr,olympic_priv->olympic_laa,dev->addr_len) ;  
		} 	
		writeb(1,init_srb+30);

		spin_lock_irqsave(&olympic_priv->olympic_lock,flags);	
		olympic_priv->srb_queued=1;

		writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);
		spin_unlock_irqrestore(&olympic_priv->olympic_lock,flags);

		t = jiffies ; 
	
		add_wait_queue(&olympic_priv->srb_wait,&wait) ;
		set_current_state(TASK_INTERRUPTIBLE) ; 
 
 		while(olympic_priv->srb_queued) {        
			schedule() ; 
        		if(signal_pending(current))	{            
				printk(KERN_WARNING "%s: Signal received in open.\n",
                			dev->name);
            			printk(KERN_WARNING "SISR=%x LISR=%x\n",
                			readl(olympic_mmio+SISR),
                			readl(olympic_mmio+LISR));
            			olympic_priv->srb_queued=0;
            			break;
        		}
			if (time_after(jiffies, t + 10*HZ)) {
				printk(KERN_WARNING "%s: SRB timed out. \n",dev->name) ; 
				olympic_priv->srb_queued=0;
				break ; 
			} 
			set_current_state(TASK_INTERRUPTIBLE) ; 
    		}
		remove_wait_queue(&olympic_priv->srb_wait,&wait) ; 
		set_current_state(TASK_RUNNING) ; 
		olympic_priv->srb_queued = 0 ; 
#if OLYMPIC_DEBUG
		printk("init_srb(%p): ",init_srb);
		for(i=0;i<20;i++)
			printk("%02x ",readb(init_srb+i));
		printk("\n");
#endif
		
		/* If we get the same return response as we set, the interrupt wasn't raised and the open
                 * timed out.
		 */

		switch (resp = readb(init_srb+2)) {
		case OLYMPIC_CLEAR_RET_CODE:
			printk(KERN_WARNING "%s: Adapter Open time out or error.\n", dev->name) ; 
			goto out;
		case 0:
			open_finished = 1;
			break;
		case 0x07:
			if (!olympic_priv->olympic_ring_speed && open_finished) { /* Autosense , first time around */
				printk(KERN_WARNING "%s: Retrying at different ring speed \n", dev->name); 
				open_finished = 0 ;  
				continue;
			}

			err = readb(init_srb+7);

			if (!olympic_priv->olympic_ring_speed && ((err & 0x0f) == 0x0d)) { 
				printk(KERN_WARNING "%s: Tried to autosense ring speed with no monitors present\n",dev->name);
				printk(KERN_WARNING "%s: Please try again with a specified ring speed \n",dev->name);
			} else {
				printk(KERN_WARNING "%s: %s - %s\n", dev->name,
					open_maj_error[(err & 0xf0) >> 4],
					open_min_error[(err & 0x0f)]);
			}
			goto out;

		case 0x32:
			printk(KERN_WARNING "%s: Invalid LAA: %pM\n",
			       dev->name, olympic_priv->olympic_laa);
			goto out;

		default:
			printk(KERN_WARNING "%s: Bad OPEN response: %x\n", dev->name, resp);
			goto out;

		}
	} while (!(open_finished)) ; /* Will only loop if ring speed mismatch re-open attempted && autosense is on */	

	if (readb(init_srb+18) & (1<<3)) 
		if (olympic_priv->olympic_message_level) 
			printk(KERN_INFO "%s: Opened in FDX Mode\n",dev->name);

	if (readb(init_srb+18) & (1<<1))
		olympic_priv->olympic_ring_speed = 100 ; 
	else if (readb(init_srb+18) & 1)
		olympic_priv->olympic_ring_speed = 16 ; 
	else
		olympic_priv->olympic_ring_speed = 4 ; 

	if (olympic_priv->olympic_message_level) 
		printk(KERN_INFO "%s: Opened in %d Mbps mode\n",dev->name, olympic_priv->olympic_ring_speed);

	olympic_priv->asb = swab16(readw(init_srb+8));
	olympic_priv->srb = swab16(readw(init_srb+10));
	olympic_priv->arb = swab16(readw(init_srb+12));
	olympic_priv->trb = swab16(readw(init_srb+16));

	olympic_priv->olympic_receive_options = 0x01 ; 
	olympic_priv->olympic_copy_all_options = 0 ; 
	
	/* setup rx ring */
	
	writel((3<<16),olympic_mmio+BMCTL_RWM); /* Ensure end of frame generated interrupts */ 

	writel(BMCTL_RX_DIS|3,olympic_mmio+BMCTL_RWM); /* Yes, this the enables RX channel */

	for(i=0;i<OLYMPIC_RX_RING_SIZE;i++) {

		struct sk_buff *skb;
		
		skb=dev_alloc_skb(olympic_priv->pkt_buf_sz);
		if(skb == NULL)
			break;

		skb->dev = dev;

		olympic_priv->olympic_rx_ring[i].buffer = cpu_to_le32(pci_map_single(olympic_priv->pdev, 
							  skb->data,olympic_priv->pkt_buf_sz, PCI_DMA_FROMDEVICE)) ; 
		olympic_priv->olympic_rx_ring[i].res_length = cpu_to_le32(olympic_priv->pkt_buf_sz); 
		olympic_priv->rx_ring_skb[i]=skb;
	}

	if (i==0) {
		printk(KERN_WARNING "%s: Not enough memory to allocate rx buffers. Adapter disabled\n",dev->name);
		goto out;
	}

	olympic_priv->rx_ring_dma_addr = pci_map_single(olympic_priv->pdev,olympic_priv->olympic_rx_ring, 
					 sizeof(struct olympic_rx_desc) * OLYMPIC_RX_RING_SIZE, PCI_DMA_TODEVICE);
	writel(olympic_priv->rx_ring_dma_addr, olympic_mmio+RXDESCQ);
	writel(olympic_priv->rx_ring_dma_addr, olympic_mmio+RXCDA);
	writew(i, olympic_mmio+RXDESCQCNT);
		
	olympic_priv->rx_status_ring_dma_addr = pci_map_single(olympic_priv->pdev, olympic_priv->olympic_rx_status_ring, 
						sizeof(struct olympic_rx_status) * OLYMPIC_RX_RING_SIZE, PCI_DMA_FROMDEVICE);
	writel(olympic_priv->rx_status_ring_dma_addr, olympic_mmio+RXSTATQ);
	writel(olympic_priv->rx_status_ring_dma_addr, olympic_mmio+RXCSA);
	
 	olympic_priv->rx_ring_last_received = OLYMPIC_RX_RING_SIZE - 1;	/* last processed rx status */
	olympic_priv->rx_status_last_received = OLYMPIC_RX_RING_SIZE - 1;  

	writew(i, olympic_mmio+RXSTATQCNT);

#if OLYMPIC_DEBUG 
	printk("# of rx buffers: %d, RXENQ: %x\n",i, readw(olympic_mmio+RXENQ));
	printk("RXCSA: %x, rx_status_ring[0]: %p\n",readl(olympic_mmio+RXCSA),&olympic_priv->olympic_rx_status_ring[0]);
	printk(" stat_ring[1]: %p, stat_ring[2]: %p, stat_ring[3]: %p\n", &(olympic_priv->olympic_rx_status_ring[1]), &(olympic_priv->olympic_rx_status_ring[2]), &(olympic_priv->olympic_rx_status_ring[3]) );
	printk(" stat_ring[4]: %p, stat_ring[5]: %p, stat_ring[6]: %p\n", &(olympic_priv->olympic_rx_status_ring[4]), &(olympic_priv->olympic_rx_status_ring[5]), &(olympic_priv->olympic_rx_status_ring[6]) );
	printk(" stat_ring[7]: %p\n", &(olympic_priv->olympic_rx_status_ring[7])  );

	printk("RXCDA: %x, rx_ring[0]: %p\n",readl(olympic_mmio+RXCDA),&olympic_priv->olympic_rx_ring[0]);
	printk("Rx_ring_dma_addr = %08x, rx_status_dma_addr = %08x\n",
		olympic_priv->rx_ring_dma_addr,olympic_priv->rx_status_ring_dma_addr) ; 
#endif

	writew((((readw(olympic_mmio+RXENQ)) & 0x8000) ^ 0x8000) | i,olympic_mmio+RXENQ);

#if OLYMPIC_DEBUG 
	printk("# of rx buffers: %d, RXENQ: %x\n",i, readw(olympic_mmio+RXENQ));
	printk("RXCSA: %x, rx_ring[0]: %p\n",readl(olympic_mmio+RXCSA),&olympic_priv->olympic_rx_status_ring[0]);
	printk("RXCDA: %x, rx_ring[0]: %p\n",readl(olympic_mmio+RXCDA),&olympic_priv->olympic_rx_ring[0]);
#endif 

	writel(SISR_RX_STATUS | SISR_RX_NOBUF,olympic_mmio+SISR_MASK_SUM);

	/* setup tx ring */

	writel(BMCTL_TX1_DIS,olympic_mmio+BMCTL_RWM); /* Yes, this enables TX channel 1 */
	for(i=0;i<OLYMPIC_TX_RING_SIZE;i++) 
		olympic_priv->olympic_tx_ring[i].buffer=cpu_to_le32(0xdeadbeef);

	olympic_priv->free_tx_ring_entries=OLYMPIC_TX_RING_SIZE;
	olympic_priv->tx_ring_dma_addr = pci_map_single(olympic_priv->pdev,olympic_priv->olympic_tx_ring,
					 sizeof(struct olympic_tx_desc) * OLYMPIC_TX_RING_SIZE,PCI_DMA_TODEVICE) ; 
	writel(olympic_priv->tx_ring_dma_addr, olympic_mmio+TXDESCQ_1);
	writel(olympic_priv->tx_ring_dma_addr, olympic_mmio+TXCDA_1);
	writew(OLYMPIC_TX_RING_SIZE, olympic_mmio+TXDESCQCNT_1);
	
	olympic_priv->tx_status_ring_dma_addr = pci_map_single(olympic_priv->pdev, olympic_priv->olympic_tx_status_ring,
						sizeof(struct olympic_tx_status) * OLYMPIC_TX_RING_SIZE, PCI_DMA_FROMDEVICE);
	writel(olympic_priv->tx_status_ring_dma_addr,olympic_mmio+TXSTATQ_1);
	writel(olympic_priv->tx_status_ring_dma_addr,olympic_mmio+TXCSA_1);
	writew(OLYMPIC_TX_RING_SIZE,olympic_mmio+TXSTATQCNT_1);
		
	olympic_priv->tx_ring_free=0; /* next entry in tx ring to use */
	olympic_priv->tx_ring_last_status=OLYMPIC_TX_RING_SIZE-1; /* last processed tx status */

	writel(0xffffffff, olympic_mmio+EISR_RWM) ; /* clean the eisr */
	writel(0,olympic_mmio+EISR) ; 
	writel(EISR_MASK_OPTIONS,olympic_mmio+EISR_MASK) ; /* enables most of the TX error interrupts */
	writel(SISR_TX1_EOF | SISR_ADAPTER_CHECK | SISR_ARB_CMD | SISR_TRB_REPLY | SISR_ASB_FREE | SISR_ERR,olympic_mmio+SISR_MASK_SUM);

#if OLYMPIC_DEBUG 
	printk("BMCTL: %x\n",readl(olympic_mmio+BMCTL_SUM));
	printk("SISR MASK: %x\n",readl(olympic_mmio+SISR_MASK));
#endif

	if (olympic_priv->olympic_network_monitor) { 
		u8 __iomem *oat;
		u8 __iomem *opt;
		u8 addr[6];
		oat = (olympic_priv->olympic_lap + olympic_priv->olympic_addr_table_addr);
		opt = (olympic_priv->olympic_lap + olympic_priv->olympic_parms_addr);

		for (i = 0; i < 6; i++)
			addr[i] = readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr)+i);
		printk("%s: Node Address: %pM\n", dev->name, addr);
		printk("%s: Functional Address: %02x:%02x:%02x:%02x\n",dev->name, 
			readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)), 
			readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+1),
			readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+2),
			readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+3));

		for (i = 0; i < 6; i++)
			addr[i] = readb(opt+offsetof(struct olympic_parameters_table, up_node_addr)+i);
		printk("%s: NAUN Address: %pM\n", dev->name, addr);
	}
	
	netif_start_queue(dev);
	return 0;

out:
	free_irq(dev->irq, dev);
	return -EIO;
}	

/*
 *	When we enter the rx routine we do not know how many frames have been 
 *	queued on the rx channel.  Therefore we start at the next rx status
 *	position and travel around the receive ring until we have completed
 *	all the frames.
 *
 *	This means that we may process the frame before we receive the end
 *	of frame interrupt. This is why we always test the status instead
 *	of blindly processing the next frame.
 *
 *	We also remove the last 4 bytes from the packet as well, these are
 *	just token ring trailer info and upset protocols that don't check 
 *	their own length, i.e. SNA. 
 *	
 */
static void olympic_rx(struct net_device *dev)
{
	struct olympic_private *olympic_priv=netdev_priv(dev);
	u8 __iomem *olympic_mmio=olympic_priv->olympic_mmio;
	struct olympic_rx_status *rx_status;
	struct olympic_rx_desc *rx_desc ; 
	int rx_ring_last_received,length, buffer_cnt, cpy_length, frag_len;
	struct sk_buff *skb, *skb2;
	int i;

	rx_status=&(olympic_priv->olympic_rx_status_ring[(olympic_priv->rx_status_last_received + 1) & (OLYMPIC_RX_RING_SIZE - 1)]) ; 
 
	while (rx_status->status_buffercnt) { 
                u32 l_status_buffercnt;

		olympic_priv->rx_status_last_received++ ;
		olympic_priv->rx_status_last_received &= (OLYMPIC_RX_RING_SIZE -1);
#if OLYMPIC_DEBUG
		printk("rx status: %x rx len: %x \n", le32_to_cpu(rx_status->status_buffercnt), le32_to_cpu(rx_status->fragmentcnt_framelen));
#endif
		length = le32_to_cpu(rx_status->fragmentcnt_framelen) & 0xffff;
		buffer_cnt = le32_to_cpu(rx_status->status_buffercnt) & 0xffff; 
		i = buffer_cnt ; /* Need buffer_cnt later for rxenq update */ 
		frag_len = le32_to_cpu(rx_status->fragmentcnt_framelen) >> 16; 

#if OLYMPIC_DEBUG 
		printk("length: %x, frag_len: %x, buffer_cnt: %x\n", length, frag_len, buffer_cnt);
#endif
                l_status_buffercnt = le32_to_cpu(rx_status->status_buffercnt);
		if(l_status_buffercnt & 0xC0000000) {
			if (l_status_buffercnt & 0x3B000000) {
				if (olympic_priv->olympic_message_level) {
					if (l_status_buffercnt & (1<<29))  /* Rx Frame Truncated */
						printk(KERN_WARNING "%s: Rx Frame Truncated \n",dev->name);
					if (l_status_buffercnt & (1<<28)) /*Rx receive overrun */
						printk(KERN_WARNING "%s: Rx Frame Receive overrun \n",dev->name);
					if (l_status_buffercnt & (1<<27)) /* No receive buffers */
						printk(KERN_WARNING "%s: No receive buffers \n",dev->name);
					if (l_status_buffercnt & (1<<25)) /* Receive frame error detect */
						printk(KERN_WARNING "%s: Receive frame error detect \n",dev->name);
					if (l_status_buffercnt & (1<<24)) /* Received Error Detect */
						printk(KERN_WARNING "%s: Received Error Detect \n",dev->name);
				} 
				olympic_priv->rx_ring_last_received += i ; 
				olympic_priv->rx_ring_last_received &= (OLYMPIC_RX_RING_SIZE -1) ; 
				dev->stats.rx_errors++;
			} else {	
			
				if (buffer_cnt == 1) {
					skb = dev_alloc_skb(max_t(int, olympic_priv->pkt_buf_sz,length)) ; 
				} else {
					skb = dev_alloc_skb(length) ; 
				}

				if (skb == NULL) {
					printk(KERN_WARNING "%s: Not enough memory to copy packet to upper layers. \n",dev->name) ;
					dev->stats.rx_dropped++;
					/* Update counters even though we don't transfer the frame */
					olympic_priv->rx_ring_last_received += i ; 
					olympic_priv->rx_ring_last_received &= (OLYMPIC_RX_RING_SIZE -1) ;  
				} else  {
					/* Optimise based upon number of buffers used. 
			   	   	   If only one buffer is used we can simply swap the buffers around.
			   	   	   If more than one then we must use the new buffer and copy the information
			   	   	   first. Ideally all frames would be in a single buffer, this can be tuned by
                               	   	   altering the buffer size. If the length of the packet is less than
					   1500 bytes we're going to copy it over anyway to stop packets getting
					   dropped from sockets with buffers smaller than our pkt_buf_sz. */
				
 					if (buffer_cnt==1) {
						olympic_priv->rx_ring_last_received++ ; 
						olympic_priv->rx_ring_last_received &= (OLYMPIC_RX_RING_SIZE -1);
						rx_ring_last_received = olympic_priv->rx_ring_last_received ;
						if (length > 1500) { 
							skb2=olympic_priv->rx_ring_skb[rx_ring_last_received] ; 
							/* unmap buffer */
							pci_unmap_single(olympic_priv->pdev,
								le32_to_cpu(olympic_priv->olympic_rx_ring[rx_ring_last_received].buffer), 
								olympic_priv->pkt_buf_sz,PCI_DMA_FROMDEVICE) ; 
							skb_put(skb2,length-4);
							skb2->protocol = tr_type_trans(skb2,dev);
							olympic_priv->olympic_rx_ring[rx_ring_last_received].buffer = 
								cpu_to_le32(pci_map_single(olympic_priv->pdev, skb->data, 
								olympic_priv->pkt_buf_sz, PCI_DMA_FROMDEVICE));
							olympic_priv->olympic_rx_ring[rx_ring_last_received].res_length = 
								cpu_to_le32(olympic_priv->pkt_buf_sz); 
							olympic_priv->rx_ring_skb[rx_ring_last_received] = skb ; 
							netif_rx(skb2) ; 
						} else { 
							pci_dma_sync_single_for_cpu(olympic_priv->pdev,
								le32_to_cpu(olympic_priv->olympic_rx_ring[rx_ring_last_received].buffer),
								olympic_priv->pkt_buf_sz,PCI_DMA_FROMDEVICE) ; 
							skb_copy_from_linear_data(olympic_priv->rx_ring_skb[rx_ring_last_received],
								      skb_put(skb,length - 4),
								      length - 4);
							pci_dma_sync_single_for_device(olympic_priv->pdev,
								le32_to_cpu(olympic_priv->olympic_rx_ring[rx_ring_last_received].buffer),
								olympic_priv->pkt_buf_sz,PCI_DMA_FROMDEVICE) ;
							skb->protocol = tr_type_trans(skb,dev) ; 
							netif_rx(skb) ; 
						} 
					} else {
						do { /* Walk the buffers */ 
							olympic_priv->rx_ring_last_received++ ; 
							olympic_priv->rx_ring_last_received &= (OLYMPIC_RX_RING_SIZE -1);
							rx_ring_last_received = olympic_priv->rx_ring_last_received ; 
							pci_dma_sync_single_for_cpu(olympic_priv->pdev,
								le32_to_cpu(olympic_priv->olympic_rx_ring[rx_ring_last_received].buffer),
								olympic_priv->pkt_buf_sz,PCI_DMA_FROMDEVICE) ; 
							rx_desc = &(olympic_priv->olympic_rx_ring[rx_ring_last_received]);
							cpy_length = (i == 1 ? frag_len : le32_to_cpu(rx_desc->res_length)); 
							skb_copy_from_linear_data(olympic_priv->rx_ring_skb[rx_ring_last_received],
								      skb_put(skb, cpy_length),
								      cpy_length);
							pci_dma_sync_single_for_device(olympic_priv->pdev,
								le32_to_cpu(olympic_priv->olympic_rx_ring[rx_ring_last_received].buffer),
								olympic_priv->pkt_buf_sz,PCI_DMA_FROMDEVICE) ;
						} while (--i) ; 
						skb_trim(skb,skb->len-4) ; 
						skb->protocol = tr_type_trans(skb,dev);
						netif_rx(skb) ; 
					} 
					dev->stats.rx_packets++ ;
					dev->stats.rx_bytes += length ;
				} /* if skb == null */
			} /* If status & 0x3b */

		} else { /*if buffercnt & 0xC */
			olympic_priv->rx_ring_last_received += i ; 
			olympic_priv->rx_ring_last_received &= (OLYMPIC_RX_RING_SIZE - 1) ; 
		} 

		rx_status->fragmentcnt_framelen = 0 ; 
		rx_status->status_buffercnt = 0 ; 
		rx_status = &(olympic_priv->olympic_rx_status_ring[(olympic_priv->rx_status_last_received+1) & (OLYMPIC_RX_RING_SIZE -1) ]);

		writew((((readw(olympic_mmio+RXENQ)) & 0x8000) ^ 0x8000) |  buffer_cnt , olympic_mmio+RXENQ); 
	} /* while */

}

static void olympic_freemem(struct net_device *dev) 
{ 
	struct olympic_private *olympic_priv=netdev_priv(dev);
	int i;
			
	for(i=0;i<OLYMPIC_RX_RING_SIZE;i++) {
		if (olympic_priv->rx_ring_skb[olympic_priv->rx_status_last_received] != NULL) {
			dev_kfree_skb_irq(olympic_priv->rx_ring_skb[olympic_priv->rx_status_last_received]);
			olympic_priv->rx_ring_skb[olympic_priv->rx_status_last_received] = NULL;
		}
		if (olympic_priv->olympic_rx_ring[olympic_priv->rx_status_last_received].buffer != cpu_to_le32(0xdeadbeef)) {
			pci_unmap_single(olympic_priv->pdev, 
			le32_to_cpu(olympic_priv->olympic_rx_ring[olympic_priv->rx_status_last_received].buffer),
			olympic_priv->pkt_buf_sz, PCI_DMA_FROMDEVICE);
		}
		olympic_priv->rx_status_last_received++;
		olympic_priv->rx_status_last_received&=OLYMPIC_RX_RING_SIZE-1;
	}
	/* unmap rings */
	pci_unmap_single(olympic_priv->pdev, olympic_priv->rx_status_ring_dma_addr, 
		sizeof(struct olympic_rx_status) * OLYMPIC_RX_RING_SIZE, PCI_DMA_FROMDEVICE);
	pci_unmap_single(olympic_priv->pdev, olympic_priv->rx_ring_dma_addr,
		sizeof(struct olympic_rx_desc) * OLYMPIC_RX_RING_SIZE, PCI_DMA_TODEVICE);

	pci_unmap_single(olympic_priv->pdev, olympic_priv->tx_status_ring_dma_addr, 
		sizeof(struct olympic_tx_status) * OLYMPIC_TX_RING_SIZE, PCI_DMA_FROMDEVICE);
	pci_unmap_single(olympic_priv->pdev, olympic_priv->tx_ring_dma_addr, 
		sizeof(struct olympic_tx_desc) * OLYMPIC_TX_RING_SIZE, PCI_DMA_TODEVICE);

	return ; 
}
 
static irqreturn_t olympic_interrupt(int irq, void *dev_id) 
{
	struct net_device *dev= (struct net_device *)dev_id;
	struct olympic_private *olympic_priv=netdev_priv(dev);
	u8 __iomem *olympic_mmio=olympic_priv->olympic_mmio;
	u32 sisr;
	u8 __iomem *adapter_check_area ; 
	
	/* 
	 *  Read sisr but don't reset it yet. 
	 *  The indication bit may have been set but the interrupt latch
	 *  bit may not be set, so we'd lose the interrupt later. 
	 */ 
	sisr=readl(olympic_mmio+SISR) ; 
	if (!(sisr & SISR_MI)) /* Interrupt isn't for us */ 
		return IRQ_NONE;
	sisr=readl(olympic_mmio+SISR_RR) ;  /* Read & Reset sisr */ 

	spin_lock(&olympic_priv->olympic_lock);

	/* Hotswap gives us this on removal */
	if (sisr == 0xffffffff) { 
		printk(KERN_WARNING "%s: Hotswap adapter removal.\n",dev->name) ; 
		spin_unlock(&olympic_priv->olympic_lock) ; 
		return IRQ_NONE;
	} 
		
	if (sisr & (SISR_SRB_REPLY | SISR_TX1_EOF | SISR_RX_STATUS | SISR_ADAPTER_CHECK |  
			SISR_ASB_FREE | SISR_ARB_CMD | SISR_TRB_REPLY | SISR_RX_NOBUF | SISR_ERR)) {  
	
		/* If we ever get this the adapter is seriously dead. Only a reset is going to 
		 * bring it back to life. We're talking pci bus errors and such like :( */ 
		if((sisr & SISR_ERR) && (readl(olympic_mmio+EISR) & EISR_MASK_OPTIONS)) {
			printk(KERN_ERR "Olympic: EISR Error, EISR=%08x\n",readl(olympic_mmio+EISR)) ; 
			printk(KERN_ERR "The adapter must be reset to clear this condition.\n") ; 
			printk(KERN_ERR "Please report this error to the driver maintainer and/\n") ; 
			printk(KERN_ERR "or the linux-tr mailing list.\n") ; 
			wake_up_interruptible(&olympic_priv->srb_wait);
			spin_unlock(&olympic_priv->olympic_lock) ; 
			return IRQ_HANDLED;
		} /* SISR_ERR */

		if(sisr & SISR_SRB_REPLY) {
			if(olympic_priv->srb_queued==1) {
				wake_up_interruptible(&olympic_priv->srb_wait);
			} else if (olympic_priv->srb_queued==2) { 
				olympic_srb_bh(dev) ; 
			}
			olympic_priv->srb_queued=0;
		} /* SISR_SRB_REPLY */

		/* We shouldn't ever miss the Tx interrupt, but the you never know, hence the loop to ensure
		   we get all tx completions. */
		if (sisr & SISR_TX1_EOF) {
			while(olympic_priv->olympic_tx_status_ring[(olympic_priv->tx_ring_last_status + 1) & (OLYMPIC_TX_RING_SIZE-1)].status) { 
				olympic_priv->tx_ring_last_status++;
				olympic_priv->tx_ring_last_status &= (OLYMPIC_TX_RING_SIZE-1);
				olympic_priv->free_tx_ring_entries++;
				dev->stats.tx_bytes += olympic_priv->tx_ring_skb[olympic_priv->tx_ring_last_status]->len;
				dev->stats.tx_packets++ ;
				pci_unmap_single(olympic_priv->pdev, 
					le32_to_cpu(olympic_priv->olympic_tx_ring[olympic_priv->tx_ring_last_status].buffer), 
					olympic_priv->tx_ring_skb[olympic_priv->tx_ring_last_status]->len,PCI_DMA_TODEVICE);
				dev_kfree_skb_irq(olympic_priv->tx_ring_skb[olympic_priv->tx_ring_last_status]);
				olympic_priv->olympic_tx_ring[olympic_priv->tx_ring_last_status].buffer=cpu_to_le32(0xdeadbeef);
				olympic_priv->olympic_tx_status_ring[olympic_priv->tx_ring_last_status].status=0;
			}
			netif_wake_queue(dev);
		} /* SISR_TX1_EOF */
	
		if (sisr & SISR_RX_STATUS) {
			olympic_rx(dev);
		} /* SISR_RX_STATUS */
	
		if (sisr & SISR_ADAPTER_CHECK) {
			netif_stop_queue(dev);
			printk(KERN_WARNING "%s: Adapter Check Interrupt Raised, 8 bytes of information follow:\n", dev->name);
			writel(readl(olympic_mmio+LAPWWC),olympic_mmio+LAPA);
			adapter_check_area = olympic_priv->olympic_lap + ((readl(olympic_mmio+LAPWWC)) & (~0xf800)) ;
			printk(KERN_WARNING "%s: Bytes %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",dev->name, readb(adapter_check_area+0), readb(adapter_check_area+1), readb(adapter_check_area+2), readb(adapter_check_area+3), readb(adapter_check_area+4), readb(adapter_check_area+5), readb(adapter_check_area+6), readb(adapter_check_area+7)) ; 
			spin_unlock(&olympic_priv->olympic_lock) ; 
			return IRQ_HANDLED; 
		} /* SISR_ADAPTER_CHECK */
	
		if (sisr & SISR_ASB_FREE) {
			/* Wake up anything that is waiting for the asb response */  
			if (olympic_priv->asb_queued) {
				olympic_asb_bh(dev) ; 
			}
		} /* SISR_ASB_FREE */
	
		if (sisr & SISR_ARB_CMD) {
			olympic_arb_cmd(dev) ; 
		} /* SISR_ARB_CMD */
	
		if (sisr & SISR_TRB_REPLY) {
			/* Wake up anything that is waiting for the trb response */
			if (olympic_priv->trb_queued) {
				wake_up_interruptible(&olympic_priv->trb_wait);
			}
			olympic_priv->trb_queued = 0 ; 
		} /* SISR_TRB_REPLY */	
	
		if (sisr & SISR_RX_NOBUF) {
			/* According to the documentation, we don't have to do anything, but trapping it keeps it out of
                  	   	   /var/log/messages.  */
		} /* SISR_RX_NOBUF */
	} else { 
		printk(KERN_WARNING "%s: Unexpected interrupt: %x\n",dev->name, sisr);
		printk(KERN_WARNING "%s: SISR_MASK: %x\n",dev->name, readl(olympic_mmio+SISR_MASK)) ;
	} /* One if the interrupts we want */
	writel(SISR_MI,olympic_mmio+SISR_MASK_SUM);
	
	spin_unlock(&olympic_priv->olympic_lock) ; 
	return IRQ_HANDLED;
}	

static int olympic_xmit(struct sk_buff *skb, struct net_device *dev) 
{
	struct olympic_private *olympic_priv=netdev_priv(dev);
	u8 __iomem *olympic_mmio=olympic_priv->olympic_mmio;
	unsigned long flags ; 

	spin_lock_irqsave(&olympic_priv->olympic_lock, flags);

	netif_stop_queue(dev);
	
	if(olympic_priv->free_tx_ring_entries) {
		olympic_priv->olympic_tx_ring[olympic_priv->tx_ring_free].buffer = 
			cpu_to_le32(pci_map_single(olympic_priv->pdev, skb->data, skb->len,PCI_DMA_TODEVICE));
		olympic_priv->olympic_tx_ring[olympic_priv->tx_ring_free].status_length = cpu_to_le32(skb->len | (0x80000000));
		olympic_priv->tx_ring_skb[olympic_priv->tx_ring_free]=skb;
		olympic_priv->free_tx_ring_entries--;

        	olympic_priv->tx_ring_free++;
        	olympic_priv->tx_ring_free &= (OLYMPIC_TX_RING_SIZE-1);
		writew((((readw(olympic_mmio+TXENQ_1)) & 0x8000) ^ 0x8000) | 1,olympic_mmio+TXENQ_1);
		netif_wake_queue(dev);
		spin_unlock_irqrestore(&olympic_priv->olympic_lock,flags);
		return 0;
	} else {
		spin_unlock_irqrestore(&olympic_priv->olympic_lock,flags);
		return 1;
	} 

}
	

static int olympic_close(struct net_device *dev) 
{
	struct olympic_private *olympic_priv=netdev_priv(dev);
	u8 __iomem *olympic_mmio=olympic_priv->olympic_mmio,*srb;
	unsigned long t,flags;

	DECLARE_WAITQUEUE(wait,current) ; 

	netif_stop_queue(dev);
	
	writel(olympic_priv->srb,olympic_mmio+LAPA);
	srb=olympic_priv->olympic_lap + (olympic_priv->srb & (~0xf800));
	
    	writeb(SRB_CLOSE_ADAPTER,srb+0);
	writeb(0,srb+1);
	writeb(OLYMPIC_CLEAR_RET_CODE,srb+2);

	add_wait_queue(&olympic_priv->srb_wait,&wait) ;
	set_current_state(TASK_INTERRUPTIBLE) ; 

	spin_lock_irqsave(&olympic_priv->olympic_lock,flags);
	olympic_priv->srb_queued=1;

	writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);
	spin_unlock_irqrestore(&olympic_priv->olympic_lock,flags);

	while(olympic_priv->srb_queued) {

		t = schedule_timeout_interruptible(60*HZ);

        	if(signal_pending(current))	{            
			printk(KERN_WARNING "%s: SRB timed out.\n",dev->name);
            		printk(KERN_WARNING "SISR=%x MISR=%x\n",readl(olympic_mmio+SISR),readl(olympic_mmio+LISR));
            		olympic_priv->srb_queued=0;
            		break;
        	}

		if (t == 0) { 
			printk(KERN_WARNING "%s: SRB timed out. May not be fatal. \n",dev->name) ; 
		} 
		olympic_priv->srb_queued=0;
    	}
	remove_wait_queue(&olympic_priv->srb_wait,&wait) ; 

	olympic_priv->rx_status_last_received++;
	olympic_priv->rx_status_last_received&=OLYMPIC_RX_RING_SIZE-1;

	olympic_freemem(dev) ; 	

	/* reset tx/rx fifo's and busmaster logic */

	writel(readl(olympic_mmio+BCTL)|(3<<13),olympic_mmio+BCTL);
	udelay(1);
	writel(readl(olympic_mmio+BCTL)&~(3<<13),olympic_mmio+BCTL);

#if OLYMPIC_DEBUG
	{
	int i ; 
	printk("srb(%p): ",srb);
	for(i=0;i<4;i++)
		printk("%x ",readb(srb+i));
	printk("\n");
	}
#endif
	free_irq(dev->irq,dev);

	return 0;
	
}

static void olympic_set_rx_mode(struct net_device *dev) 
{
	struct olympic_private *olympic_priv = netdev_priv(dev);
   	u8 __iomem *olympic_mmio = olympic_priv->olympic_mmio ; 
	u8 options = 0; 
	u8 __iomem *srb;
	struct dev_mc_list *dmi ; 
	unsigned char dev_mc_address[4] ; 
	int i ; 

	writel(olympic_priv->srb,olympic_mmio+LAPA);
	srb=olympic_priv->olympic_lap + (olympic_priv->srb & (~0xf800));
	options = olympic_priv->olympic_copy_all_options; 

	if (dev->flags&IFF_PROMISC)  
		options |= 0x61 ;
	else
		options &= ~0x61 ; 

	/* Only issue the srb if there is a change in options */

	if ((options ^ olympic_priv->olympic_copy_all_options)) { 
	
		/* Now to issue the srb command to alter the copy.all.options */
	
		writeb(SRB_MODIFY_RECEIVE_OPTIONS,srb);
		writeb(0,srb+1);
		writeb(OLYMPIC_CLEAR_RET_CODE,srb+2);
		writeb(0,srb+3);
		writeb(olympic_priv->olympic_receive_options,srb+4);
		writeb(options,srb+5);

		olympic_priv->srb_queued=2; /* Can't sleep, use srb_bh */

		writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);

		olympic_priv->olympic_copy_all_options = options ;
		
		return ;  
	} 

	/* Set the functional addresses we need for multicast */

	dev_mc_address[0] = dev_mc_address[1] = dev_mc_address[2] = dev_mc_address[3] = 0 ; 

	for (i=0,dmi=dev->mc_list;i < dev->mc_count; i++,dmi = dmi->next) { 
		dev_mc_address[0] |= dmi->dmi_addr[2] ; 
		dev_mc_address[1] |= dmi->dmi_addr[3] ; 
		dev_mc_address[2] |= dmi->dmi_addr[4] ; 
		dev_mc_address[3] |= dmi->dmi_addr[5] ; 
	}

	writeb(SRB_SET_FUNC_ADDRESS,srb+0);
	writeb(0,srb+1);
	writeb(OLYMPIC_CLEAR_RET_CODE,srb+2);
	writeb(0,srb+3);
	writeb(0,srb+4);
	writeb(0,srb+5);
	writeb(dev_mc_address[0],srb+6);
	writeb(dev_mc_address[1],srb+7);
	writeb(dev_mc_address[2],srb+8);
	writeb(dev_mc_address[3],srb+9);

	olympic_priv->srb_queued = 2 ;
	writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);

}

static void olympic_srb_bh(struct net_device *dev) 
{ 
	struct olympic_private *olympic_priv = netdev_priv(dev);
   	u8 __iomem *olympic_mmio = olympic_priv->olympic_mmio ; 
	u8 __iomem *srb;

	writel(olympic_priv->srb,olympic_mmio+LAPA);
	srb=olympic_priv->olympic_lap + (olympic_priv->srb & (~0xf800));

	switch (readb(srb)) { 

		/* SRB_MODIFY_RECEIVE_OPTIONS i.e. set_multicast_list options (promiscuous) 
                 * At some point we should do something if we get an error, such as
                 * resetting the IFF_PROMISC flag in dev
		 */

		case SRB_MODIFY_RECEIVE_OPTIONS:
			switch (readb(srb+2)) { 
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command\n",dev->name) ; 
					break ; 
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name);
					break ; 
				default:
					if (olympic_priv->olympic_message_level) 
						printk(KERN_WARNING "%s: Receive Options Modified to %x,%x\n",dev->name,olympic_priv->olympic_copy_all_options, olympic_priv->olympic_receive_options) ; 
					break ; 	
			} /* switch srb[2] */ 
			break ;
		
		/* SRB_SET_GROUP_ADDRESS - Multicast group setting 
                 */

		case SRB_SET_GROUP_ADDRESS:
			switch (readb(srb+2)) { 
				case 0x00:
					break ; 
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command \n",dev->name) ; 
					break ;
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name); 
					break ;
				case 0x3c:
					printk(KERN_WARNING "%s: Group/Functional address indicator bits not set correctly\n",dev->name) ; 
					break ;
				case 0x3e: /* If we ever implement individual multicast addresses, will need to deal with this */
					printk(KERN_WARNING "%s: Group address registers full\n",dev->name) ; 
					break ;  
				case 0x55:
					printk(KERN_INFO "%s: Group Address already set.\n",dev->name) ; 
					break ;
				default:
					break ; 
			} /* switch srb[2] */ 
			break ; 

		/* SRB_RESET_GROUP_ADDRESS - Remove a multicast address from group list
 		 */

		case SRB_RESET_GROUP_ADDRESS:
			switch (readb(srb+2)) { 
				case 0x00:
					break ; 
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command \n",dev->name) ; 
					break ; 
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name) ; 
					break ; 
				case 0x39: /* Must deal with this if individual multicast addresses used */
					printk(KERN_INFO "%s: Group address not found \n",dev->name); 
					break ;
				default:
					break ; 
			} /* switch srb[2] */
			break ; 

		
		/* SRB_SET_FUNC_ADDRESS - Called by the set_rx_mode 
		 */

		case SRB_SET_FUNC_ADDRESS:
			switch (readb(srb+2)) { 
				case 0x00:
					if (olympic_priv->olympic_message_level)
						printk(KERN_INFO "%s: Functional Address Mask Set \n",dev->name) ; 
					break ;
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command \n",dev->name) ; 
					break ; 
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name) ; 
					break ; 
				default:
					break ; 
			} /* switch srb[2] */
			break ; 
	
		/* SRB_READ_LOG - Read and reset the adapter error counters
 		 */

		case SRB_READ_LOG:
			switch (readb(srb+2)) { 
				case 0x00: 
					if (olympic_priv->olympic_message_level) 
						printk(KERN_INFO "%s: Read Log issued\n",dev->name) ; 
					break ; 
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command \n",dev->name) ; 
					break ; 
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name) ; 
					break ; 
			
			} /* switch srb[2] */
			break ; 
		
		/* SRB_READ_SR_COUNTERS - Read and reset the source routing bridge related counters */

		case SRB_READ_SR_COUNTERS:
			switch (readb(srb+2)) { 
				case 0x00: 
					if (olympic_priv->olympic_message_level) 
						printk(KERN_INFO "%s: Read Source Routing Counters issued\n",dev->name) ; 
					break ; 
				case 0x01:
					printk(KERN_WARNING "%s: Unrecognized srb command \n",dev->name) ; 
					break ; 
				case 0x04:
					printk(KERN_WARNING "%s: Adapter must be open for this operation, doh!!\n",dev->name) ; 
					break ; 
				default:
					break ; 
			} /* switch srb[2] */
			break ;
 
		default:
			printk(KERN_WARNING "%s: Unrecognized srb bh return value.\n",dev->name);
			break ; 
	} /* switch srb[0] */

} 

static int olympic_set_mac_address (struct net_device *dev, void *addr) 
{
	struct sockaddr *saddr = addr ; 
	struct olympic_private *olympic_priv = netdev_priv(dev);

	if (netif_running(dev)) { 
		printk(KERN_WARNING "%s: Cannot set mac/laa address while card is open\n", dev->name) ; 
		return -EIO ; 
	}

	memcpy(olympic_priv->olympic_laa, saddr->sa_data,dev->addr_len) ; 
	
	if (olympic_priv->olympic_message_level) { 
 		printk(KERN_INFO "%s: MAC/LAA Set to  = %x.%x.%x.%x.%x.%x\n",dev->name, olympic_priv->olympic_laa[0],
		olympic_priv->olympic_laa[1], olympic_priv->olympic_laa[2],
		olympic_priv->olympic_laa[3], olympic_priv->olympic_laa[4],
		olympic_priv->olympic_laa[5]);
	} 

	return 0 ; 
}

static void olympic_arb_cmd(struct net_device *dev)
{
	struct olympic_private *olympic_priv = netdev_priv(dev);
	u8 __iomem *olympic_mmio=olympic_priv->olympic_mmio;
	u8 __iomem *arb_block, *asb_block, *srb  ; 
	u8 header_len ; 
	u16 frame_len, buffer_len ;
	struct sk_buff *mac_frame ;  
	u8 __iomem *buf_ptr ;
	u8 __iomem *frame_data ;  
	u16 buff_off ; 
	u16 lan_status = 0, lan_status_diff  ; /* Initialize to stop compiler warning */
	u8 fdx_prot_error ; 
	u16 next_ptr;

	arb_block = (olympic_priv->olympic_lap + olympic_priv->arb) ; 
	asb_block = (olympic_priv->olympic_lap + olympic_priv->asb) ; 
	srb = (olympic_priv->olympic_lap + olympic_priv->srb) ; 
	
	if (readb(arb_block+0) == ARB_RECEIVE_DATA) { /* Receive.data, MAC frames */

		header_len = readb(arb_block+8) ; /* 802.5 Token-Ring Header Length */	
		frame_len = swab16(readw(arb_block + 10)) ; 

		buff_off = swab16(readw(arb_block + 6)) ;
		
		buf_ptr = olympic_priv->olympic_lap + buff_off ; 

#if OLYMPIC_DEBUG
{
		int i;
		frame_data = buf_ptr+offsetof(struct mac_receive_buffer,frame_data) ; 

		for (i=0 ;  i < 14 ; i++) { 
			printk("Loc %d = %02x\n",i,readb(frame_data + i)); 
		}

		printk("next %04x, fs %02x, len %04x \n",readw(buf_ptr+offsetof(struct mac_receive_buffer,next)), readb(buf_ptr+offsetof(struct mac_receive_buffer,frame_status)), readw(buf_ptr+offsetof(struct mac_receive_buffer,buffer_length))); 
}
#endif 
		mac_frame = dev_alloc_skb(frame_len) ; 
		if (!mac_frame) {
			printk(KERN_WARNING "%s: Memory squeeze, dropping frame.\n", dev->name);
			goto drop_frame;
		}

		/* Walk the buffer chain, creating the frame */

		do {
			frame_data = buf_ptr+offsetof(struct mac_receive_buffer,frame_data) ; 
			buffer_len = swab16(readw(buf_ptr+offsetof(struct mac_receive_buffer,buffer_length))); 
			memcpy_fromio(skb_put(mac_frame, buffer_len), frame_data , buffer_len ) ;
			next_ptr=readw(buf_ptr+offsetof(struct mac_receive_buffer,next)); 
		} while (next_ptr && (buf_ptr=olympic_priv->olympic_lap + swab16(next_ptr)));

		mac_frame->protocol = tr_type_trans(mac_frame, dev);

		if (olympic_priv->olympic_network_monitor) { 
			struct trh_hdr *mac_hdr;
			printk(KERN_WARNING "%s: Received MAC Frame, details: \n",dev->name);
			mac_hdr = tr_hdr(mac_frame);
			printk(KERN_WARNING "%s: MAC Frame Dest. Addr: %pM\n",
			       dev->name, mac_hdr->daddr);
			printk(KERN_WARNING "%s: MAC Frame Srce. Addr: %pM\n",
			       dev->name, mac_hdr->saddr);
		}
		netif_rx(mac_frame);

drop_frame:
		/* Now tell the card we have dealt with the received frame */

		/* Set LISR Bit 1 */
		writel(LISR_ARB_FREE,olympic_priv->olympic_mmio + LISR_SUM);

		/* Is the ASB free ? */ 	
		
		if (readb(asb_block + 2) != 0xff) { 
			olympic_priv->asb_queued = 1 ; 
			writel(LISR_ASB_FREE_REQ,olympic_priv->olympic_mmio+LISR_SUM); 
			return ; 	
			/* Drop out and wait for the bottom half to be run */
		}
		
		writeb(ASB_RECEIVE_DATA,asb_block); /* Receive data */
		writeb(OLYMPIC_CLEAR_RET_CODE,asb_block+2); /* Necessary ?? */
		writeb(readb(arb_block+6),asb_block+6); /* Must send the address back to the adapter */
		writeb(readb(arb_block+7),asb_block+7); /* To let it know we have dealt with the data */		

		writel(LISR_ASB_REPLY | LISR_ASB_FREE_REQ,olympic_priv->olympic_mmio+LISR_SUM);
		
		olympic_priv->asb_queued = 2 ; 
	
		return ; 	
		
	} else if (readb(arb_block) == ARB_LAN_CHANGE_STATUS) { /* Lan.change.status */
		lan_status = swab16(readw(arb_block+6));
		fdx_prot_error = readb(arb_block+8) ; 
		
		/* Issue ARB Free */
		writel(LISR_ARB_FREE,olympic_priv->olympic_mmio+LISR_SUM);

		lan_status_diff = olympic_priv->olympic_lan_status ^ lan_status ; 

		if (lan_status_diff & (LSC_LWF | LSC_ARW | LSC_FPE | LSC_RR) ) { 
			if (lan_status_diff & LSC_LWF) 
					printk(KERN_WARNING "%s: Short circuit detected on the lobe\n",dev->name);
			if (lan_status_diff & LSC_ARW) 
					printk(KERN_WARNING "%s: Auto removal error\n",dev->name);
			if (lan_status_diff & LSC_FPE)
					printk(KERN_WARNING "%s: FDX Protocol Error\n",dev->name);
			if (lan_status_diff & LSC_RR) 
					printk(KERN_WARNING "%s: Force remove MAC frame received\n",dev->name);
		
			/* Adapter has been closed by the hardware */
		
			/* reset tx/rx fifo's and busmaster logic */

			writel(readl(olympic_mmio+BCTL)|(3<<13),olympic_mmio+BCTL);
			udelay(1);
			writel(readl(olympic_mmio+BCTL)&~(3<<13),olympic_mmio+BCTL);
			netif_stop_queue(dev);
			olympic_priv->srb = readw(olympic_priv->olympic_lap + LAPWWO) ; 
			printk(KERN_WARNING "%s: Adapter has been closed \n", dev->name) ; 
		} /* If serious error */
		
		if (olympic_priv->olympic_message_level) { 
			if (lan_status_diff & LSC_SIG_LOSS) 
					printk(KERN_WARNING "%s: No receive signal detected \n", dev->name) ; 
			if (lan_status_diff & LSC_HARD_ERR)
					printk(KERN_INFO "%s: Beaconing \n",dev->name);
			if (lan_status_diff & LSC_SOFT_ERR)
					printk(KERN_WARNING "%s: Adapter transmitted Soft Error Report Mac Frame \n",dev->name);
			if (lan_status_diff & LSC_TRAN_BCN) 
					printk(KERN_INFO "%s: We are tranmitting the beacon, aaah\n",dev->name);
			if (lan_status_diff & LSC_SS) 
					printk(KERN_INFO "%s: Single Station on the ring \n", dev->name);
			if (lan_status_diff & LSC_RING_REC)
					printk(KERN_INFO "%s: Ring recovery ongoing\n",dev->name);
			if (lan_status_diff & LSC_FDX_MODE)
					printk(KERN_INFO "%s: Operating in FDX mode\n",dev->name);
		} 	
		
		if (lan_status_diff & LSC_CO) { 
					
				if (olympic_priv->olympic_message_level) 
					printk(KERN_INFO "%s: Counter Overflow \n", dev->name);
					
				/* Issue READ.LOG command */

				writeb(SRB_READ_LOG, srb);
				writeb(0,srb+1);
				writeb(OLYMPIC_CLEAR_RET_CODE,srb+2);
				writeb(0,srb+3);
				writeb(0,srb+4);
				writeb(0,srb+5);
					
				olympic_priv->srb_queued=2; /* Can't sleep, use srb_bh */

				writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);
					
		}

		if (lan_status_diff & LSC_SR_CO) { 

				if (olympic_priv->olympic_message_level)
					printk(KERN_INFO "%s: Source routing counters overflow\n", dev->name);

				/* Issue a READ.SR.COUNTERS */
				
				writeb(SRB_READ_SR_COUNTERS,srb);
				writeb(0,srb+1);
				writeb(OLYMPIC_CLEAR_RET_CODE,srb+2);
				writeb(0,srb+3);
				
				olympic_priv->srb_queued=2; /* Can't sleep, use srb_bh */

				writel(LISR_SRB_CMD,olympic_mmio+LISR_SUM);

		}

		olympic_priv->olympic_lan_status = lan_status ; 
	
	}  /* Lan.change.status */
	else
		printk(KERN_WARNING "%s: Unknown arb command \n", dev->name);
}

static void olympic_asb_bh(struct net_device *dev) 
{
	struct olympic_private *olympic_priv = netdev_priv(dev);
	u8 __iomem *arb_block, *asb_block ; 

	arb_block = (olympic_priv->olympic_lap + olympic_priv->arb) ; 
	asb_block = (olympic_priv->olympic_lap + olympic_priv->asb) ; 

	if (olympic_priv->asb_queued == 1) {   /* Dropped through the first time */

		writeb(ASB_RECEIVE_DATA,asb_block); /* Receive data */
		writeb(OLYMPIC_CLEAR_RET_CODE,asb_block+2); /* Necessary ?? */
		writeb(readb(arb_block+6),asb_block+6); /* Must send the address back to the adapter */
		writeb(readb(arb_block+7),asb_block+7); /* To let it know we have dealt with the data */		

		writel(LISR_ASB_REPLY | LISR_ASB_FREE_REQ,olympic_priv->olympic_mmio+LISR_SUM);
		olympic_priv->asb_queued = 2 ; 

		return ; 
	}

	if (olympic_priv->asb_queued == 2) { 
		switch (readb(asb_block+2)) {
			case 0x01:
				printk(KERN_WARNING "%s: Unrecognized command code \n", dev->name);
				break ;
			case 0x26:
				printk(KERN_WARNING "%s: Unrecognized buffer address \n", dev->name);
				break ;
			case 0xFF:
				/* Valid response, everything should be ok again */
				break ;
			default:
				printk(KERN_WARNING "%s: Invalid return code in asb\n",dev->name);
				break ;
		}
	}
	olympic_priv->asb_queued = 0 ; 
}
 
static int olympic_change_mtu(struct net_device *dev, int mtu) 
{
	struct olympic_private *olympic_priv = netdev_priv(dev);
	u16 max_mtu ; 

	if (olympic_priv->olympic_ring_speed == 4)
		max_mtu = 4500 ; 
	else
		max_mtu = 18000 ; 
	
	if (mtu > max_mtu)
		return -EINVAL ; 
	if (mtu < 100) 
		return -EINVAL ; 

	dev->mtu = mtu ; 
	olympic_priv->pkt_buf_sz = mtu + TR_HLEN ; 

	return 0 ; 
}

static int olympic_proc_info(char *buffer, char **start, off_t offset, int length, int *eof, void *data)
{
	struct net_device *dev = (struct net_device *)data ; 
	struct olympic_private *olympic_priv=netdev_priv(dev);
	u8 __iomem *oat = (olympic_priv->olympic_lap + olympic_priv->olympic_addr_table_addr) ; 
	u8 __iomem *opt = (olympic_priv->olympic_lap + olympic_priv->olympic_parms_addr) ; 
	int size = 0 ; 
	int len=0;
	off_t begin=0;
	off_t pos=0;
	u8 addr[6];
	u8 addr2[6];
	int i;

	size = sprintf(buffer, 
		"IBM Pit/Pit-Phy/Olympic Chipset Token Ring Adapter %s\n",dev->name);
	size += sprintf(buffer+size, "\n%6s: Adapter Address   : Node Address      : Functional Addr\n",
 	   dev->name); 

	for (i = 0 ; i < 6 ; i++)
		addr[i] = readb(oat+offsetof(struct olympic_adapter_addr_table,node_addr) + i);

	size += sprintf(buffer+size, "%6s: %pM : %pM : %02x:%02x:%02x:%02x\n",
	   dev->name,
	   dev->dev_addr, addr,
	   readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)), 
	   readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+1),
	   readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+2),
	   readb(oat+offsetof(struct olympic_adapter_addr_table,func_addr)+3));
	 
	size += sprintf(buffer+size, "\n%6s: Token Ring Parameters Table:\n", dev->name);

	size += sprintf(buffer+size, "%6s: Physical Addr : Up Node Address   : Poll Address      : AccPri : Auth Src : Att Code :\n",
	  dev->name) ; 

	for (i = 0 ; i < 6 ; i++)
		addr[i] = readb(opt+offsetof(struct olympic_parameters_table, up_node_addr) + i);
	for (i = 0 ; i < 6 ; i++)
		addr2[i] =  readb(opt+offsetof(struct olympic_parameters_table, poll_addr) + i);

	size += sprintf(buffer+size, "%6s: %02x:%02x:%02x:%02x   : %pM : %pM : %04x   : %04x     :  %04x    :\n",
	  dev->name,
	  readb(opt+offsetof(struct olympic_parameters_table, phys_addr)),
	  readb(opt+offsetof(struct olympic_parameters_table, phys_addr)+1),
	  readb(opt+offsetof(struct olympic_parameters_table, phys_addr)+2),
	  readb(opt+offsetof(struct olympic_parameters_table, phys_addr)+3),
	  addr, addr2,
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, acc_priority))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, auth_source_class))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, att_code))));

	size += sprintf(buffer+size, "%6s: Source Address    : Bcn T : Maj. V : Lan St : Lcl Rg : Mon Err : Frame Correl : \n",
	  dev->name) ; 
	
	for (i = 0 ; i < 6 ; i++)
		addr[i] = readb(opt+offsetof(struct olympic_parameters_table, source_addr) + i);
	size += sprintf(buffer+size, "%6s: %pM : %04x  : %04x   : %04x   : %04x   : %04x    :     %04x     : \n",
	  dev->name, addr,
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, beacon_type))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, major_vector))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, lan_status))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, local_ring))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, mon_error))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, frame_correl))));

	size += sprintf(buffer+size, "%6s: Beacon Details :  Tx  :  Rx  : NAUN Node Address : NAUN Node Phys : \n",
	  dev->name) ; 

	for (i = 0 ; i < 6 ; i++)
		addr[i] = readb(opt+offsetof(struct olympic_parameters_table, beacon_naun) + i);
	size += sprintf(buffer+size, "%6s:                :  %02x  :  %02x  : %pM : %02x:%02x:%02x:%02x    : \n",
	  dev->name,
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, beacon_transmit))),
	  swab16(readw(opt+offsetof(struct olympic_parameters_table, beacon_receive))),
	  addr,
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_phys)),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_phys)+1),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_phys)+2),
	  readb(opt+offsetof(struct olympic_parameters_table, beacon_phys)+3));

	len=size;
	pos=begin+size;
	if (pos<offset) {
		len=0;
		begin=pos;
	}
	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Start slop */
	if(len>length)
		len=length;		/* Ending slop */
	return len;
}

static void __devexit olympic_remove_one(struct pci_dev *pdev) 
{
	struct net_device *dev = pci_get_drvdata(pdev) ; 
	struct olympic_private *olympic_priv=netdev_priv(dev);

	if (olympic_priv->olympic_network_monitor) { 
		char proc_name[20] ; 
		strcpy(proc_name,"olympic_") ;
		strcat(proc_name,dev->name) ;
		remove_proc_entry(proc_name,init_net.proc_net);
	}
	unregister_netdev(dev) ; 
	iounmap(olympic_priv->olympic_mmio) ; 
	iounmap(olympic_priv->olympic_lap) ; 
	pci_release_regions(pdev) ;
	pci_set_drvdata(pdev,NULL) ;  	
	free_netdev(dev) ; 
}

static struct pci_driver olympic_driver = { 
	.name		= "olympic",
	.id_table	= olympic_pci_tbl,
	.probe		= olympic_probe,
	.remove		= __devexit_p(olympic_remove_one),
};

static int __init olympic_pci_init(void) 
{
	return pci_register_driver(&olympic_driver) ;
}

static void __exit olympic_pci_cleanup(void)
{
	pci_unregister_driver(&olympic_driver) ; 
}	


module_init(olympic_pci_init) ; 
module_exit(olympic_pci_cleanup) ; 

MODULE_LICENSE("GPL");
