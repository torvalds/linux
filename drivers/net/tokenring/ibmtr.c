/* ibmtr.c:  A shared-memory IBM Token Ring 16/4 driver for linux
 *
 *	Written 1993 by Mark Swanson and Peter De Schrijver.
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License, incorporated herein by reference.
 *
 *	This device driver should work with Any IBM Token Ring Card that does
 *	not use DMA.
 *
 *	I used Donald Becker's (becker@scyld.com) device driver work
 *	as a base for most of my initial work.
 *
 *	Changes by Peter De Schrijver
 *		(Peter.Deschrijver@linux.cc.kuleuven.ac.be) :
 *
 *	+ changed name to ibmtr.c in anticipation of other tr boards.
 *	+ changed reset code and adapter open code.
 *	+ added SAP open code.
 *	+ a first attempt to write interrupt, transmit and receive routines.
 *
 *	Changes by David W. Morris (dwm@shell.portal.com) :
 *	941003 dwm: - Restructure tok_probe for multiple adapters, devices.
 *	+ Add comments, misc reorg for clarity.
 *	+ Flatten interrupt handler levels.
 *
 *	Changes by Farzad Farid (farzy@zen.via.ecp.fr)
 *	and Pascal Andre (andre@chimay.via.ecp.fr) (March 9 1995) :
 *	+ multi ring support clean up.
 *	+ RFC1042 compliance enhanced.
 *
 *	Changes by Pascal Andre (andre@chimay.via.ecp.fr) (September 7 1995) :
 *	+ bug correction in tr_tx
 *	+ removed redundant information display
 *	+ some code reworking
 *
 *	Changes by Michel Lespinasse (walken@via.ecp.fr),
 *	Yann Doussot (doussot@via.ecp.fr) and Pascal Andre (andre@via.ecp.fr)
 *	(February 18, 1996) :
 *	+ modified shared memory and mmio access port the driver to
 *	  alpha platform (structure access -> readb/writeb)
 *
 *	Changes by Steve Kipisz (bungy@ibm.net or kipisz@vnet.ibm.com)
 *	(January 18 1996):
 *	+ swapped WWOR and WWCR in ibmtr.h
 *	+ moved some init code from tok_probe into trdev_init.  The
 *	  PCMCIA code can call trdev_init to complete initializing
 *	  the driver.
 *	+ added -DPCMCIA to support PCMCIA
 *	+ detecting PCMCIA Card Removal in interrupt handler.  If
 *	  ISRP is FF, then a PCMCIA card has been removed
 *        10/2000 Burt needed a new method to avoid crashing the OS
 *
 *	Changes by Paul Norton (pnorton@cts.com) :
 *	+ restructured the READ.LOG logic to prevent the transmit SRB
 *	  from being rudely overwritten before the transmit cycle is
 *	  complete. (August 15 1996)
 *	+ completed multiple adapter support. (November 20 1996)
 *	+ implemented csum_partial_copy in tr_rx and increased receive 
 *        buffer size and count. Minor fixes. (March 15, 1997)
 *
 *	Changes by Christopher Turcksin <wabbit@rtfc.demon.co.uk>
 *	+ Now compiles ok as a module again.
 *
 *	Changes by Paul Norton (pnorton@ieee.org) :
 *      + moved the header manipulation code in tr_tx and tr_rx to
 *        net/802/tr.c. (July 12 1997)
 *      + add retry and timeout on open if cable disconnected. (May 5 1998)
 *      + lifted 2000 byte mtu limit. now depends on shared-RAM size.
 *        May 25 1998)
 *      + can't allocate 2k recv buff at 8k shared-RAM. (20 October 1998)
 *
 *      Changes by Joel Sloan (jjs@c-me.com) :
 *      + disable verbose debug messages by default - to enable verbose
 *	  debugging, edit the IBMTR_DEBUG_MESSAGES define below 
 *	
 *	Changes by Mike Phillips <phillim@amtrak.com> :
 *	+ Added extra #ifdef's to work with new PCMCIA Token Ring Code.
 *	  The PCMCIA code now just sets up the card so it can be recognized
 *        by ibmtr_probe. Also checks allocated memory vs. on-board memory
 *	  for correct figure to use.
 *
 *	Changes by Tim Hockin (thockin@isunix.it.ilstu.edu) :
 *	+ added spinlocks for SMP sanity (10 March 1999)
 *
 *      Changes by Jochen Friedrich to enable RFC1469 Option 2 multicasting
 *      i.e. using functional address C0 00 00 04 00 00 to transmit and 
 *      receive multicast packets.
 *
 *      Changes by Mike Sullivan (based on original sram patch by Dave Grothe
 *      to support windowing into on adapter shared ram.
 *      i.e. Use LANAID to setup a PnP configuration with 16K RAM. Paging
 *      will shift this 16K window over the entire available shared RAM.
 *
 *      Changes by Peter De Schrijver (p2@mind.be) :
 *      + fixed a problem with PCMCIA card removal
 *
 *      Change by Mike Sullivan et al.:
 *      + added turbo card support. No need to use lanaid to configure
 *      the adapter into isa compatiblity mode.
 *
 *      Changes by Burt Silverman to allow the computer to behave nicely when
 *	a cable is pulled or not in place, or a PCMCIA card is removed hot.
 */

/* change the define of IBMTR_DEBUG_MESSAGES to a nonzero value 
in the event that chatty debug messages are desired - jjs 12/30/98 */

#define IBMTR_DEBUG_MESSAGES 0

#include <linux/module.h>

#ifdef PCMCIA		/* required for ibmtr_cs.c to build */
#undef MODULE		/* yes, really */
#undef ENABLE_PAGING
#else
#define ENABLE_PAGING 1		
#endif

/* changes the output format of driver initialization */
#define TR_VERBOSE	0

/* some 95 OS send many non UI frame; this allow removing the warning */
#define TR_FILTERNONUI	1

#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/trdevice.h>
#include <linux/ibmtr.h>

#include <net/checksum.h>

#include <asm/io.h>

#define DPRINTK(format, args...) printk("%s: " format, dev->name , ## args)
#define DPRINTD(format, args...) DummyCall("%s: " format, dev->name , ## args)

/* version and credits */
#ifndef PCMCIA
static char version[] __devinitdata =
    "\nibmtr.c: v1.3.57   8/ 7/94 Peter De Schrijver and Mark Swanson\n"
    "         v2.1.125 10/20/98 Paul Norton    <pnorton@ieee.org>\n"
    "         v2.2.0   12/30/98 Joel Sloan     <jjs@c-me.com>\n"
    "         v2.2.1   02/08/00 Mike Sullivan  <sullivam@us.ibm.com>\n" 
    "         v2.2.2   07/27/00 Burt Silverman <burts@us.ibm.com>\n" 
    "         v2.4.0   03/01/01 Mike Sullivan <sullivan@us.ibm.com>\n";
#endif

/* this allows displaying full adapter information */

static char *channel_def[] __devinitdata = { "ISA", "MCA", "ISA P&P" };

static char pcchannelid[] __devinitdata = {
	0x05, 0x00, 0x04, 0x09,
	0x04, 0x03, 0x04, 0x0f,
	0x03, 0x06, 0x03, 0x01,
	0x03, 0x01, 0x03, 0x00,
	0x03, 0x09, 0x03, 0x09,
	0x03, 0x00, 0x02, 0x00
};

static char mcchannelid[] __devinitdata =  {
	0x04, 0x0d, 0x04, 0x01,
	0x05, 0x02, 0x05, 0x03,
	0x03, 0x06, 0x03, 0x03,
	0x05, 0x08, 0x03, 0x04,
	0x03, 0x05, 0x03, 0x01,
	0x03, 0x08, 0x02, 0x00
};

static char __devinit *adapter_def(char type)
{
	switch (type) {
	case 0xF: return "PC Adapter | PC Adapter II | Adapter/A";
	case 0xE: return "16/4 Adapter | 16/4 Adapter/A (long)";
	case 0xD: return "16/4 Adapter/A (short) | 16/4 ISA-16 Adapter";
	case 0xC: return "Auto 16/4 Adapter";
	default: return "adapter (unknown type)";
	};
};

#define TRC_INIT 0x01		/*  Trace initialization & PROBEs */
#define TRC_INITV 0x02		/*  verbose init trace points     */
static unsigned char ibmtr_debug_trace = 0;

static int	ibmtr_probe1(struct net_device *dev, int ioaddr);
static unsigned char get_sram_size(struct tok_info *adapt_info);
static int 	trdev_init(struct net_device *dev);
static int 	tok_open(struct net_device *dev);
static int 	tok_init_card(struct net_device *dev);
static void	tok_open_adapter(unsigned long dev_addr);
static void 	open_sap(unsigned char type, struct net_device *dev);
static void 	tok_set_multicast_list(struct net_device *dev);
static int 	tok_send_packet(struct sk_buff *skb, struct net_device *dev);
static int 	tok_close(struct net_device *dev);
static irqreturn_t tok_interrupt(int irq, void *dev_id);
static void 	initial_tok_int(struct net_device *dev);
static void 	tr_tx(struct net_device *dev);
static void 	tr_rx(struct net_device *dev);
static void	ibmtr_reset_timer(struct timer_list*tmr,struct net_device *dev);
static void	tok_rerun(unsigned long dev_addr);
static void	ibmtr_readlog(struct net_device *dev);
static struct 	net_device_stats *tok_get_stats(struct net_device *dev);
static int	ibmtr_change_mtu(struct net_device *dev, int mtu);
static void	find_turbo_adapters(int *iolist);

static int ibmtr_portlist[IBMTR_MAX_ADAPTERS+1] __devinitdata = {
	0xa20, 0xa24, 0, 0, 0
};
static int __devinitdata turbo_io[IBMTR_MAX_ADAPTERS] = {0};
static int __devinitdata turbo_irq[IBMTR_MAX_ADAPTERS] = {0};
static int __devinitdata turbo_searched = 0;

#ifndef PCMCIA
static __u32 ibmtr_mem_base __devinitdata = 0xd0000;
#endif

static void __devinit PrtChanID(char *pcid, short stride)
{
	short i, j;
	for (i = 0, j = 0; i < 24; i++, j += stride)
		printk("%1x", ((int) pcid[j]) & 0x0f);
	printk("\n");
}

static void __devinit HWPrtChanID(void __iomem *pcid, short stride)
{
	short i, j;
	for (i = 0, j = 0; i < 24; i++, j += stride)
		printk("%1x", ((int) readb(pcid + j)) & 0x0f);
	printk("\n");
}

/* We have to ioremap every checked address, because isa_readb is 
 * going away. 
 */

static void __devinit find_turbo_adapters(int *iolist)
{
	int ram_addr;
	int index=0;
	void __iomem *chanid;
	int found_turbo=0;
	unsigned char *tchanid, ctemp;
	int i, j;
	unsigned long jif;
	void __iomem *ram_mapped ;   

	if (turbo_searched == 1) return;
	turbo_searched=1;
	for (ram_addr=0xC0000; ram_addr < 0xE0000; ram_addr+=0x2000) {

		__u32 intf_tbl=0;

		found_turbo=1;
		ram_mapped = ioremap((u32)ram_addr,0x1fff) ; 
		if (ram_mapped==NULL) 
 			continue ; 
		chanid=(CHANNEL_ID + ram_mapped);
		tchanid=pcchannelid;
		ctemp=readb(chanid) & 0x0f;
		if (ctemp != *tchanid) continue;
		for (i=2,j=1; i<=46; i=i+2,j++) {
			if ((readb(chanid+i) & 0x0f) != tchanid[j]){
				found_turbo=0;
				break;
			}
		}
		if (!found_turbo) continue;

		writeb(0x90, ram_mapped+0x1E01);
		for(i=2; i<0x0f; i++) {
			writeb(0x00, ram_mapped+0x1E01+i);
		}
		writeb(0x00, ram_mapped+0x1E01);
		for(jif=jiffies+TR_BUSY_INTERVAL; time_before_eq(jiffies,jif););
		intf_tbl=ntohs(readw(ram_mapped+ACA_OFFSET+ACA_RW+WRBR_EVEN));
		if (intf_tbl) {
#if IBMTR_DEBUG_MESSAGES
			printk("ibmtr::find_turbo_adapters, Turbo found at "
				"ram_addr %x\n",ram_addr);
			printk("ibmtr::find_turbo_adapters, interface_table ");
			for(i=0; i<6; i++) {
				printk("%x:",readb(ram_addr+intf_tbl+i));
			}
			printk("\n");
#endif
			turbo_io[index]=ntohs(readw(ram_mapped+intf_tbl+4));
			turbo_irq[index]=readb(ram_mapped+intf_tbl+3);
			outb(0, turbo_io[index] + ADAPTRESET);
			for(jif=jiffies+TR_RST_TIME;time_before_eq(jiffies,jif););
			outb(0, turbo_io[index] + ADAPTRESETREL);
			index++;
			continue;
		}
#if IBMTR_DEBUG_MESSAGES 
		printk("ibmtr::find_turbo_adapters, ibmtr card found at"
			" %x but not a Turbo model\n",ram_addr);
#endif
	iounmap(ram_mapped) ; 	
	} /* for */
	for(i=0; i<IBMTR_MAX_ADAPTERS; i++) {
		if(!turbo_io[i]) break;
		for (j=0; j<IBMTR_MAX_ADAPTERS; j++) {
			if ( iolist[j] && iolist[j] != turbo_io[i]) continue;
			iolist[j]=turbo_io[i];
			break;
		}
	}
}

static void ibmtr_cleanup_card(struct net_device *dev)
{
	if (dev->base_addr) {
		outb(0,dev->base_addr+ADAPTRESET);
		
		schedule_timeout_uninterruptible(TR_RST_TIME); /* wait 50ms */

		outb(0,dev->base_addr+ADAPTRESETREL);
	}

#ifndef PCMCIA
	free_irq(dev->irq, dev);
	release_region(dev->base_addr, IBMTR_IO_EXTENT);

	{ 
		struct tok_info *ti = netdev_priv(dev);
		iounmap(ti->mmio);
		iounmap(ti->sram_virt);
	}
#endif		
}

/****************************************************************************
 *	ibmtr_probe():  Routine specified in the network device structure
 *	to probe for an IBM Token Ring Adapter.  Routine outline:
 *	I.    Interrogate hardware to determine if an adapter exists
 *	      and what the speeds and feeds are
 *	II.   Setup data structures to control execution based upon
 *	      adapter characteristics.
 *
 *	We expect ibmtr_probe to be called once for each device entry
 *	which references it.
 ****************************************************************************/

static int __devinit ibmtr_probe(struct net_device *dev)
{
	int i;
	int base_addr = dev->base_addr;

	if (base_addr && base_addr <= 0x1ff) /* Don't probe at all. */
		return -ENXIO;
	if (base_addr > 0x1ff) { /* Check a single specified location.  */
		if (!ibmtr_probe1(dev, base_addr)) return 0;
		return -ENODEV;
	}
	find_turbo_adapters(ibmtr_portlist);
	for (i = 0; ibmtr_portlist[i]; i++) {
		int ioaddr = ibmtr_portlist[i];

		if (!ibmtr_probe1(dev, ioaddr)) return 0;
	}
	return -ENODEV;
}

int __devinit ibmtr_probe_card(struct net_device *dev)
{
	int err = ibmtr_probe(dev);
	if (!err) {
		err = register_netdev(dev);
		if (err)
			ibmtr_cleanup_card(dev);
	}
	return err;
}

/*****************************************************************************/

static int __devinit ibmtr_probe1(struct net_device *dev, int PIOaddr)
{

	unsigned char segment, intr=0, irq=0, i, j, cardpresent=NOTOK, temp=0;
	void __iomem * t_mmio = NULL;
	struct tok_info *ti = netdev_priv(dev);
	void __iomem *cd_chanid;
	unsigned char *tchanid, ctemp;
#ifndef PCMCIA
	unsigned char t_irq=0;
        unsigned long timeout;
	static int version_printed;
#endif
	DECLARE_MAC_BUF(mac);

	/*    Query the adapter PIO base port which will return
	 *    indication of where MMIO was placed. We also have a
	 *    coded interrupt number.
	 */
	segment = inb(PIOaddr);
	if (segment < 0x40 || segment > 0xe0) {
		/* Out of range values so we'll assume non-existent IO device
		 * but this is not necessarily a problem, esp if a turbo
		 * adapter is being used.  */
#if IBMTR_DEBUG_MESSAGES
		DPRINTK("ibmtr_probe1(): unhappy that inb(0x%X) == 0x%X, "
			"Hardware Problem?\n",PIOaddr,segment);
#endif
		return -ENODEV;
	}
	/*
	 *    Compute the linear base address of the MMIO area
	 *    as LINUX doesn't care about segments
	 */
	t_mmio = ioremap(((__u32) (segment & 0xfc) << 11) + 0x80000,2048);
	if (!t_mmio) { 
		DPRINTK("Cannot remap mmiobase memory area") ; 
		return -ENODEV ; 
	} 
	intr = segment & 0x03;	/* low bits is coded interrupt # */
	if (ibmtr_debug_trace & TRC_INIT)
		DPRINTK("PIOaddr: %4hx seg/intr: %2x mmio base: %p intr: %d\n"
				, PIOaddr, (int) segment, t_mmio, (int) intr);

	/*
	 *    Now we will compare expected 'channelid' strings with
	 *    what we is there to learn of ISA/MCA or not TR card
	 */
#ifdef PCMCIA
	iounmap(t_mmio);
	t_mmio = ti->mmio;	/*BMS to get virtual address */
	irq = ti->irq;		/*BMS to display the irq!   */
#endif
	cd_chanid = (CHANNEL_ID + t_mmio);	/* for efficiency */
	tchanid = pcchannelid;
	cardpresent = TR_ISA;	/* try ISA */

	/*    Suboptimize knowing first byte different */
	ctemp = readb(cd_chanid) & 0x0f;
	if (ctemp != *tchanid) {	/* NOT ISA card, try MCA */
		tchanid = mcchannelid;
		cardpresent = TR_MCA;
		if (ctemp != *tchanid)	/* Neither ISA nor MCA */
			cardpresent = NOTOK;
	}
	if (cardpresent != NOTOK) {
		/*       Know presumed type, try rest of ID */
		for (i = 2, j = 1; i <= 46; i = i + 2, j++) {
			if( (readb(cd_chanid+i)&0x0f) == tchanid[j]) continue;
			/* match failed, not TR card */
			cardpresent = NOTOK;
			break;
		}
	}
	/* 
	 *    If we have an ISA board check for the ISA P&P version,
	 *    as it has different IRQ settings 
	 */
	if (cardpresent == TR_ISA && (readb(AIPFID + t_mmio) == 0x0e))
		cardpresent = TR_ISAPNP;
	if (cardpresent == NOTOK) {	/* "channel_id" did not match, report */
		if (!(ibmtr_debug_trace & TRC_INIT)) {
#ifndef PCMCIA
			iounmap(t_mmio);
#endif
			return -ENODEV;
		}
		DPRINTK( "Channel ID string not found for PIOaddr: %4hx\n",
								PIOaddr);
		DPRINTK("Expected for ISA: ");
		PrtChanID(pcchannelid, 1);
		DPRINTK("           found: ");
/* BMS Note that this can be misleading, when hardware is flaky, because you
   are reading it a second time here. So with my flaky hardware, I'll see my-
   self in this block, with the HW ID matching the ISA ID exactly! */
		HWPrtChanID(cd_chanid, 2);
		DPRINTK("Expected for MCA: ");
		PrtChanID(mcchannelid, 1);
	}
	/* Now, setup some of the pl0 buffers for this driver.. */
	/* If called from PCMCIA, it is already set up, so no need to 
	   waste the memory, just use the existing structure */
#ifndef PCMCIA
	ti->mmio = t_mmio;
        for (i = 0; i < IBMTR_MAX_ADAPTERS; i++) {
                if (turbo_io[i] != PIOaddr)
			continue;
#if IBMTR_DEBUG_MESSAGES 
		printk("ibmtr::tr_probe1, setting PIOaddr %x to Turbo\n",
		       PIOaddr);
#endif
		ti->turbo = 1;
		t_irq = turbo_irq[i];
        }
#endif /* !PCMCIA */
	ti->readlog_pending = 0;
	init_waitqueue_head(&ti->wait_for_reset);

	/* if PCMCIA, the card can be recognized as either TR_ISA or TR_ISAPNP
	 * depending which card is inserted.	*/
	
#ifndef PCMCIA
	switch (cardpresent) {
	case TR_ISA:
		if (intr == 0) irq = 9;	/* irq2 really is irq9 */
		if (intr == 1) irq = 3;
		if (intr == 2) irq = 6;
		if (intr == 3) irq = 7;
		ti->adapter_int_enable = PIOaddr + ADAPTINTREL;
		break;
	case TR_MCA:
		if (intr == 0) irq = 9;
		if (intr == 1) irq = 3;
		if (intr == 2) irq = 10;
		if (intr == 3) irq = 11;
		ti->global_int_enable = 0;
		ti->adapter_int_enable = 0;
		ti->sram_phys=(__u32)(inb(PIOaddr+ADAPTRESETREL) & 0xfe) << 12;
		break;
	case TR_ISAPNP:
		if (!t_irq) {
			if (intr == 0) irq = 9;
			if (intr == 1) irq = 3;
			if (intr == 2) irq = 10;
			if (intr == 3) irq = 11;
		} else
			irq=t_irq;
		timeout = jiffies + TR_SPIN_INTERVAL;
		while (!readb(ti->mmio + ACA_OFFSET + ACA_RW + RRR_EVEN)){
			if (!time_after(jiffies, timeout)) continue;
			DPRINTK( "Hardware timeout during initialization.\n");
			iounmap(t_mmio);
			return -ENODEV;
		}
		ti->sram_phys =
		     ((__u32)readb(ti->mmio+ACA_OFFSET+ACA_RW+RRR_EVEN)<<12);
		ti->adapter_int_enable = PIOaddr + ADAPTINTREL;
		break;
	} /*end switch (cardpresent) */
#endif	/*not PCMCIA */

	if (ibmtr_debug_trace & TRC_INIT) {	/* just report int */
		DPRINTK("irq=%d", irq);
		printk(", sram_phys=0x%x", ti->sram_phys);
		if(ibmtr_debug_trace&TRC_INITV){ /* full chat in verbose only */
			DPRINTK(", ti->mmio=%p", ti->mmio);
			printk(", segment=%02X", segment);
		}
		printk(".\n");
	}

	/* Get hw address of token ring card */
	j = 0;
	for (i = 0; i < 0x18; i = i + 2) {
		/* technical reference states to do this */
		temp = readb(ti->mmio + AIP + i) & 0x0f;
		ti->hw_address[j] = temp;
		if (j & 1)
			dev->dev_addr[(j / 2)] =
				ti->hw_address[j]+ (ti->hw_address[j - 1] << 4);
		++j;
	}
	/* get Adapter type:  'F' = Adapter/A, 'E' = 16/4 Adapter II,... */
	ti->adapter_type = readb(ti->mmio + AIPADAPTYPE);

	/* get Data Rate:  F=4Mb, E=16Mb, D=4Mb & 16Mb ?? */
	ti->data_rate = readb(ti->mmio + AIPDATARATE);

	/* Get Early Token Release support?: F=no, E=4Mb, D=16Mb, C=4&16Mb */
	ti->token_release = readb(ti->mmio + AIPEARLYTOKEN);

	/* How much shared RAM is on adapter ? */
	if (ti->turbo) {
		ti->avail_shared_ram=127;
	} else {
		ti->avail_shared_ram = get_sram_size(ti);/*in 512 byte units */
	}
	/* We need to set or do a bunch of work here based on previous results*/
	/* Support paging?  What sizes?:  F=no, E=16k, D=32k, C=16 & 32k */
	ti->shared_ram_paging = readb(ti->mmio + AIPSHRAMPAGE);

	/* Available DHB  4Mb size:   F=2048, E=4096, D=4464 */
	switch (readb(ti->mmio + AIP4MBDHB)) {
	case 0xe: ti->dhb_size4mb = 4096; break;
	case 0xd: ti->dhb_size4mb = 4464; break;
	default:  ti->dhb_size4mb = 2048; break;
	}

	/* Available DHB 16Mb size:  F=2048, E=4096, D=8192, C=16384, B=17960 */
	switch (readb(ti->mmio + AIP16MBDHB)) {
	case 0xe: ti->dhb_size16mb = 4096; break;
	case 0xd: ti->dhb_size16mb = 8192; break;
	case 0xc: ti->dhb_size16mb = 16384; break;
	case 0xb: ti->dhb_size16mb = 17960; break;
	default:  ti->dhb_size16mb = 2048; break;
	}

	/*    We must figure out how much shared memory space this adapter
	 *    will occupy so that if there are two adapters we can fit both
	 *    in.  Given a choice, we will limit this adapter to 32K.  The
	 *    maximum space will will use for two adapters is 64K so if the
	 *    adapter we are working on demands 64K (it also doesn't support
	 *    paging), then only one adapter can be supported.  
	 */

	/*
	 *    determine how much of total RAM is mapped into PC space 
	 */
	ti->mapped_ram_size= /*sixteen to onehundredtwentyeight 512byte blocks*/
	    1<< ((readb(ti->mmio+ACA_OFFSET+ACA_RW+RRR_ODD) >> 2 & 0x03) + 4);
	ti->page_mask = 0;
	if (ti->turbo)  ti->page_mask=0xf0;
	else if (ti->shared_ram_paging == 0xf);  /* No paging in adapter */
	else {
#ifdef ENABLE_PAGING
		unsigned char pg_size = 0;
		/* BMS:   page size: PCMCIA, use configuration register;
		   ISAPNP, use LANAIDC config tool from www.ibm.com  */
		switch (ti->shared_ram_paging) {
		case 0xf:
			break;
		case 0xe:
			ti->page_mask = (ti->mapped_ram_size == 32) ? 0xc0 : 0;
			pg_size = 32;	/* 16KB page size */
			break;
		case 0xd:
			ti->page_mask = (ti->mapped_ram_size == 64) ? 0x80 : 0;
			pg_size = 64;	/* 32KB page size */
			break;
		case 0xc:
			switch (ti->mapped_ram_size) {
			case 32:
				ti->page_mask = 0xc0;
				pg_size = 32;
				break;
			case 64:
				ti->page_mask = 0x80;
				pg_size = 64;
				break;
			}
			break;
		default:
			DPRINTK("Unknown shared ram paging info %01X\n",
							ti->shared_ram_paging);
			iounmap(t_mmio); 
			return -ENODEV;
			break;
		} /*end switch shared_ram_paging */

		if (ibmtr_debug_trace & TRC_INIT)
			DPRINTK("Shared RAM paging code: %02X, "
				"mapped RAM size: %dK, shared RAM size: %dK, "
				"page mask: %02X\n:",
				ti->shared_ram_paging, ti->mapped_ram_size / 2,
				ti->avail_shared_ram / 2, ti->page_mask);
#endif	/*ENABLE_PAGING */
	}

#ifndef PCMCIA
	/* finish figuring the shared RAM address */
	if (cardpresent == TR_ISA) {
		static __u32 ram_bndry_mask[] =
			{ 0xffffe000, 0xffffc000, 0xffff8000, 0xffff0000 };
		__u32 new_base, rrr_32, chk_base, rbm;

		rrr_32=readb(ti->mmio+ACA_OFFSET+ACA_RW+RRR_ODD) >> 2 & 0x03;
		rbm = ram_bndry_mask[rrr_32];
		new_base = (ibmtr_mem_base + (~rbm)) & rbm;/* up to boundary */
		chk_base = new_base + (ti->mapped_ram_size << 9);
		if (chk_base > (ibmtr_mem_base + IBMTR_SHARED_RAM_SIZE)) {
			DPRINTK("Shared RAM for this adapter (%05x) exceeds "
			"driver limit (%05x), adapter not started.\n",
			chk_base, ibmtr_mem_base + IBMTR_SHARED_RAM_SIZE);
			iounmap(t_mmio);
			return -ENODEV;
		} else { /* seems cool, record what we have figured out */
			ti->sram_base = new_base >> 12;
			ibmtr_mem_base = chk_base;
		}
	}
	else  ti->sram_base = ti->sram_phys >> 12;

	/* The PCMCIA has already got the interrupt line and the io port, 
	   so no chance of anybody else getting it - MLP */
	if (request_irq(dev->irq = irq, &tok_interrupt, 0, "ibmtr", dev) != 0) {
		DPRINTK("Could not grab irq %d.  Halting Token Ring driver.\n",
					irq);
		iounmap(t_mmio);
		return -ENODEV;
	}
	/*?? Now, allocate some of the PIO PORTs for this driver.. */
	/* record PIOaddr range as busy */
	if (!request_region(PIOaddr, IBMTR_IO_EXTENT, "ibmtr")) {
		DPRINTK("Could not grab PIO range. Halting driver.\n");
		free_irq(dev->irq, dev);
		iounmap(t_mmio);
		return -EBUSY;
	}

	if (!version_printed++) {
		printk(version);
	}
#endif /* !PCMCIA */
	DPRINTK("%s %s found\n",
		channel_def[cardpresent - 1], adapter_def(ti->adapter_type));
	DPRINTK("using irq %d, PIOaddr %hx, %dK shared RAM.\n",
			irq, PIOaddr, ti->mapped_ram_size / 2);
	DPRINTK("Hardware address : %s\n",
		print_mac(mac, dev->dev_addr));
	if (ti->page_mask)
		DPRINTK("Shared RAM paging enabled. "
			"Page size: %uK Shared Ram size %dK\n",
			((ti->page_mask^0xff)+1) >>2, ti->avail_shared_ram / 2);
	else
		DPRINTK("Shared RAM paging disabled. ti->page_mask %x\n",
								ti->page_mask);

	/* Calculate the maximum DHB we can use */
	/* two cases where avail_shared_ram doesn't equal mapped_ram_size:
	    1. avail_shared_ram is 127 but mapped_ram_size is 128 (typical)
	    2. user has configured adapter for less than avail_shared_ram
	       but is not using paging (she should use paging, I believe)
	*/
	if (!ti->page_mask) {
		ti->avail_shared_ram=
				min(ti->mapped_ram_size,ti->avail_shared_ram);
	}

	switch (ti->avail_shared_ram) {
	case 16:		/* 8KB shared RAM */
		ti->dhb_size4mb = min(ti->dhb_size4mb, (unsigned short)2048);
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4=2;
		ti->dhb_size16mb = min(ti->dhb_size16mb, (unsigned short)2048);
		ti->rbuf_len16 = 1032;
		ti->rbuf_cnt16=2;
		break;
	case 32:		/* 16KB shared RAM */
		ti->dhb_size4mb = min(ti->dhb_size4mb, (unsigned short)4464);
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4=4;
		ti->dhb_size16mb = min(ti->dhb_size16mb, (unsigned short)4096);
		ti->rbuf_len16 = 1032;	/*1024 usable */
		ti->rbuf_cnt16=4;
		break;
	case 64:		/* 32KB shared RAM */
		ti->dhb_size4mb = min(ti->dhb_size4mb, (unsigned short)4464);
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4=6;
		ti->dhb_size16mb = min(ti->dhb_size16mb, (unsigned short)10240);
		ti->rbuf_len16 = 1032;
		ti->rbuf_cnt16=6;
		break;
	case 127:		/* 63.5KB shared RAM */
		ti->dhb_size4mb = min(ti->dhb_size4mb, (unsigned short)4464);
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4=6;
		ti->dhb_size16mb = min(ti->dhb_size16mb, (unsigned short)16384);
		ti->rbuf_len16 = 1032;
		ti->rbuf_cnt16=16;
		break;
	case 128:		/* 64KB   shared RAM */
		ti->dhb_size4mb = min(ti->dhb_size4mb, (unsigned short)4464);
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4=6;
		ti->dhb_size16mb = min(ti->dhb_size16mb, (unsigned short)17960);
		ti->rbuf_len16 = 1032;
		ti->rbuf_cnt16=16;
		break;
	default:
		ti->dhb_size4mb = 2048;
		ti->rbuf_len4 = 1032;
		ti->rbuf_cnt4=2;
		ti->dhb_size16mb = 2048;
		ti->rbuf_len16 = 1032;
		ti->rbuf_cnt16=2;
		break;
	}
	/* this formula is not smart enough for the paging case
	ti->rbuf_cnt<x> = (ti->avail_shared_ram * BLOCKSZ - ADAPT_PRIVATE -
			ARBLENGTH - SSBLENGTH - DLC_MAX_SAP * SAPLENGTH -
			DLC_MAX_STA * STALENGTH - ti->dhb_size<x>mb * NUM_DHB -
			SRBLENGTH - ASBLENGTH) / ti->rbuf_len<x>;
	*/
	ti->maxmtu16 = (ti->rbuf_len16 - 8) * ti->rbuf_cnt16  - TR_HLEN;
	ti->maxmtu4 = (ti->rbuf_len4 - 8) * ti->rbuf_cnt4 - TR_HLEN;
	/*BMS assuming 18 bytes of Routing Information (usually works) */
	DPRINTK("Maximum Receive Internet Protocol MTU 16Mbps: %d, 4Mbps: %d\n",
						     ti->maxmtu16, ti->maxmtu4);

	dev->base_addr = PIOaddr;	/* set the value for device */
	dev->mem_start = ti->sram_base << 12;
	dev->mem_end = dev->mem_start + (ti->mapped_ram_size << 9) - 1;
	trdev_init(dev);
	return 0;   /* Return 0 to indicate we have found a Token Ring card. */
}				/*ibmtr_probe1() */

/*****************************************************************************/

/* query the adapter for the size of shared RAM  */
/* the function returns the RAM size in units of 512 bytes */

static unsigned char __devinit get_sram_size(struct tok_info *adapt_info)
{
	unsigned char avail_sram_code;
	static unsigned char size_code[] = { 0, 16, 32, 64, 127, 128 };
	/* Adapter gives
	   'F' -- use RRR bits 3,2
	   'E' -- 8kb   'D' -- 16kb
	   'C' -- 32kb  'A' -- 64KB
	   'B' - 64KB less 512 bytes at top
	   (WARNING ... must zero top bytes in INIT */

	avail_sram_code = 0xf - readb(adapt_info->mmio + AIPAVAILSHRAM);
	if (avail_sram_code) return size_code[avail_sram_code];
	else		/* for code 'F', must compute size from RRR(3,2) bits */
		return 1 <<
		 ((readb(adapt_info->mmio+ACA_OFFSET+ACA_RW+RRR_ODD)>>2&3)+4);
}

/*****************************************************************************/

static int __devinit trdev_init(struct net_device *dev)
{
	struct tok_info *ti = netdev_priv(dev);

	SET_PAGE(ti->srb_page);
        ti->open_failure = NO    ;
	dev->open = tok_open;
	dev->stop = tok_close;
	dev->hard_start_xmit = tok_send_packet;
	dev->get_stats = tok_get_stats;
	dev->set_multicast_list = tok_set_multicast_list;
	dev->change_mtu = ibmtr_change_mtu;

	return 0;
}

/*****************************************************************************/

static int tok_init_card(struct net_device *dev)
{
	struct tok_info *ti;
	short PIOaddr;
	unsigned long i;

	PIOaddr = dev->base_addr;
	ti = netdev_priv(dev);
	/* Special processing for first interrupt after reset */
	ti->do_tok_int = FIRST_INT;
	/* Reset adapter */
	writeb(~INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_EVEN);
	outb(0, PIOaddr + ADAPTRESET);

	schedule_timeout_uninterruptible(TR_RST_TIME); /* wait 50ms */

	outb(0, PIOaddr + ADAPTRESETREL);
#ifdef ENABLE_PAGING
	if (ti->page_mask)
		writeb(SRPR_ENABLE_PAGING,ti->mmio+ACA_OFFSET+ACA_RW+SRPR_EVEN);
#endif
	writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN);
	i = sleep_on_timeout(&ti->wait_for_reset, 4 * HZ);
	return i? 0 : -EAGAIN;
}

/*****************************************************************************/
static int tok_open(struct net_device *dev)
{
	struct tok_info *ti = netdev_priv(dev);
	int i;

	/*the case we were left in a failure state during a previous open */
	if (ti->open_failure == YES) {
		DPRINTK("Last time you were disconnected, how about now?\n");
		printk("You can't insert with an ICS connector half-cocked.\n");
	}

	ti->open_status  = CLOSED; /* CLOSED or OPEN      */
	ti->sap_status   = CLOSED; /* CLOSED or OPEN      */
	ti->open_failure =     NO; /* NO     or YES       */
	ti->open_mode    = MANUAL; /* MANUAL or AUTOMATIC */

	ti->sram_phys &= ~1; /* to reverse what we do in tok_close */
	/* init the spinlock */
	spin_lock_init(&ti->lock);
	init_timer(&ti->tr_timer);
	
	i = tok_init_card(dev);
	if (i) return i;

	while (1){
		tok_open_adapter((unsigned long) dev);
		i= interruptible_sleep_on_timeout(&ti->wait_for_reset, 25 * HZ);
		/* sig catch: estimate opening adapter takes more than .5 sec*/
		if (i>(245*HZ)/10) break; /* fancier than if (i==25*HZ) */
		if (i==0) break;
		if (ti->open_status == OPEN && ti->sap_status==OPEN) {
			netif_start_queue(dev);
			DPRINTK("Adapter is up and running\n");
			return 0;
		}
		i=schedule_timeout_interruptible(TR_RETRY_INTERVAL);
							/* wait 30 seconds */
		if(i!=0) break; /*prob. a signal, like the i>24*HZ case above */
	}
	outb(0, dev->base_addr + ADAPTRESET);/* kill pending interrupts*/
	DPRINTK("TERMINATED via signal\n");	/*BMS useful */
	return -EAGAIN;
}

/*****************************************************************************/

#define COMMAND_OFST             0
#define OPEN_OPTIONS_OFST        8
#define NUM_RCV_BUF_OFST        24
#define RCV_BUF_LEN_OFST        26
#define DHB_LENGTH_OFST         28
#define NUM_DHB_OFST            30
#define DLC_MAX_SAP_OFST        32
#define DLC_MAX_STA_OFST        33

static void tok_open_adapter(unsigned long dev_addr)
{
	struct net_device *dev = (struct net_device *) dev_addr;
	struct tok_info *ti;
	int i;

	ti = netdev_priv(dev);
	SET_PAGE(ti->init_srb_page); 
	writeb(~SRB_RESP_INT, ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_ODD);
	for (i = 0; i < sizeof(struct dir_open_adapter); i++)
		writeb(0, ti->init_srb + i);
	writeb(DIR_OPEN_ADAPTER, ti->init_srb + COMMAND_OFST);
	writew(htons(OPEN_PASS_BCON_MAC), ti->init_srb + OPEN_OPTIONS_OFST);
	if (ti->ring_speed == 16) {
		writew(htons(ti->dhb_size16mb), ti->init_srb + DHB_LENGTH_OFST);
		writew(htons(ti->rbuf_cnt16), ti->init_srb + NUM_RCV_BUF_OFST);
		writew(htons(ti->rbuf_len16), ti->init_srb + RCV_BUF_LEN_OFST);
	} else {
		writew(htons(ti->dhb_size4mb), ti->init_srb + DHB_LENGTH_OFST);
		writew(htons(ti->rbuf_cnt4), ti->init_srb + NUM_RCV_BUF_OFST);
		writew(htons(ti->rbuf_len4), ti->init_srb + RCV_BUF_LEN_OFST);
	}
	writeb(NUM_DHB,		/* always 2 */ ti->init_srb + NUM_DHB_OFST);
	writeb(DLC_MAX_SAP, ti->init_srb + DLC_MAX_SAP_OFST);
	writeb(DLC_MAX_STA, ti->init_srb + DLC_MAX_STA_OFST);
	ti->srb = ti->init_srb;	/* We use this one in the interrupt handler */
	ti->srb_page = ti->init_srb_page;
	DPRINTK("Opening adapter: Xmit bfrs: %d X %d, Rcv bfrs: %d X %d\n",
		readb(ti->init_srb + NUM_DHB_OFST),
		ntohs(readw(ti->init_srb + DHB_LENGTH_OFST)),
		ntohs(readw(ti->init_srb + NUM_RCV_BUF_OFST)),
		ntohs(readw(ti->init_srb + RCV_BUF_LEN_OFST)));
	writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN);
	writeb(CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
}

/*****************************************************************************/

static void open_sap(unsigned char type, struct net_device *dev)
{
	int i;
	struct tok_info *ti = netdev_priv(dev);

	SET_PAGE(ti->srb_page);
	for (i = 0; i < sizeof(struct dlc_open_sap); i++)
		writeb(0, ti->srb + i);

#define MAX_I_FIELD_OFST        14
#define SAP_VALUE_OFST          16
#define SAP_OPTIONS_OFST        17
#define STATION_COUNT_OFST      18

	writeb(DLC_OPEN_SAP, ti->srb + COMMAND_OFST);
	writew(htons(MAX_I_FIELD), ti->srb + MAX_I_FIELD_OFST);
	writeb(SAP_OPEN_IND_SAP | SAP_OPEN_PRIORITY, ti->srb+ SAP_OPTIONS_OFST);
	writeb(SAP_OPEN_STATION_CNT, ti->srb + STATION_COUNT_OFST);
	writeb(type, ti->srb + SAP_VALUE_OFST);
	writeb(CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
}


/*****************************************************************************/

static void tok_set_multicast_list(struct net_device *dev)
{
	struct tok_info *ti = netdev_priv(dev);
	struct dev_mc_list *mclist;
	unsigned char address[4];

	int i;

	/*BMS the next line is CRUCIAL or you may be sad when you */
	/*BMS ifconfig tr down or hot unplug a PCMCIA card ??hownowbrowncow*/
	if (/*BMSHELPdev->start == 0 ||*/ ti->open_status != OPEN) return;
	address[0] = address[1] = address[2] = address[3] = 0;
	mclist = dev->mc_list;
	for (i = 0; i < dev->mc_count; i++) {
		address[0] |= mclist->dmi_addr[2];
		address[1] |= mclist->dmi_addr[3];
		address[2] |= mclist->dmi_addr[4];
		address[3] |= mclist->dmi_addr[5];
		mclist = mclist->next;
	}
	SET_PAGE(ti->srb_page);
	for (i = 0; i < sizeof(struct srb_set_funct_addr); i++)
		writeb(0, ti->srb + i);

#define FUNCT_ADDRESS_OFST 6

	writeb(DIR_SET_FUNC_ADDR, ti->srb + COMMAND_OFST);
	for (i = 0; i < 4; i++) 
		writeb(address[i], ti->srb + FUNCT_ADDRESS_OFST + i);
	writeb(CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
#if TR_VERBOSE
	DPRINTK("Setting functional address: ");
	for (i=0;i<4;i++)  printk("%02X ", address[i]);
	printk("\n");
#endif
}

/*****************************************************************************/

#define STATION_ID_OFST 4

static int tok_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct tok_info *ti;
	unsigned long flags;
	ti = netdev_priv(dev);

        netif_stop_queue(dev);

	/* lock against other CPUs */
	spin_lock_irqsave(&(ti->lock), flags);

	/* Save skb; we'll need it when the adapter asks for the data */
	ti->current_skb = skb;
	SET_PAGE(ti->srb_page);
	writeb(XMIT_UI_FRAME, ti->srb + COMMAND_OFST);
	writew(ti->exsap_station_id, ti->srb + STATION_ID_OFST);
	writeb(CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
	spin_unlock_irqrestore(&(ti->lock), flags);
	dev->trans_start = jiffies;
	return 0;
}

/*****************************************************************************/

static int tok_close(struct net_device *dev)
{
	struct tok_info *ti = netdev_priv(dev);

	/* Important for PCMCIA hot unplug, otherwise, we'll pull the card, */
	/* unloading the module from memory, and then if a timer pops, ouch */
	del_timer_sync(&ti->tr_timer);
	outb(0, dev->base_addr + ADAPTRESET);
	ti->sram_phys |= 1;
	ti->open_status = CLOSED;

	netif_stop_queue(dev);
	DPRINTK("Adapter is closed.\n");
	return 0;
}

/*****************************************************************************/

#define RETCODE_OFST		2
#define OPEN_ERROR_CODE_OFST	6
#define ASB_ADDRESS_OFST        8
#define SRB_ADDRESS_OFST        10
#define ARB_ADDRESS_OFST        12
#define SSB_ADDRESS_OFST        14

static char *printphase[]= {"Lobe media test","Physical insertion",
	      "Address verification","Roll call poll","Request Parameters"};
static char *printerror[]={"Function failure","Signal loss","Reserved",
		"Frequency error","Timeout","Ring failure","Ring beaconing",
		"Duplicate node address",
		"Parameter request-retry count exceeded","Remove received",
		"IMPL force received","Duplicate modifier",
		"No monitor detected","Monitor contention failed for RPL"};

static void __iomem *map_address(struct tok_info *ti, unsigned index, __u8 *page)
{
	if (ti->page_mask) {
		*page = (index >> 8) & ti->page_mask;
		index &= ~(ti->page_mask << 8);
	}
	return ti->sram_virt + index;
}

static void dir_open_adapter (struct net_device *dev)
{
        struct tok_info *ti = netdev_priv(dev);
        unsigned char ret_code;
        __u16 err;

        ti->srb = map_address(ti,
		ntohs(readw(ti->init_srb + SRB_ADDRESS_OFST)),
		&ti->srb_page);
        ti->ssb = map_address(ti,
		ntohs(readw(ti->init_srb + SSB_ADDRESS_OFST)),
		&ti->ssb_page);
        ti->arb = map_address(ti,
		ntohs(readw(ti->init_srb + ARB_ADDRESS_OFST)),
		&ti->arb_page);
        ti->asb = map_address(ti,
		ntohs(readw(ti->init_srb + ASB_ADDRESS_OFST)),
		&ti->asb_page);
        ti->current_skb = NULL;
        ret_code = readb(ti->init_srb + RETCODE_OFST);
        err = ntohs(readw(ti->init_srb + OPEN_ERROR_CODE_OFST));
        if (!ret_code) {
		ti->open_status = OPEN; /* TR adapter is now available */
                if (ti->open_mode == AUTOMATIC) {
			DPRINTK("Adapter reopened.\n");
                }
                writeb(~SRB_RESP_INT, ti->mmio+ACA_OFFSET+ACA_RESET+ISRP_ODD);
                open_sap(EXTENDED_SAP, dev);
		return;
	}
	ti->open_failure = YES;
	if (ret_code == 7){
               if (err == 0x24) {
			if (!ti->auto_speedsave) {
				DPRINTK("Open failed: Adapter speed must match "
                                 "ring speed if Automatic Ring Speed Save is "
				 "disabled.\n");
				ti->open_action = FAIL;
			}else
				DPRINTK("Retrying open to adjust to "
					"ring speed, ");
                } else if (err == 0x2d) {
			DPRINTK("Physical Insertion: No Monitor Detected, ");
			printk("retrying after %ds delay...\n",
					TR_RETRY_INTERVAL/HZ);
                } else if (err == 0x11) {
			DPRINTK("Lobe Media Function Failure (0x11), ");
			printk(" retrying after %ds delay...\n",
					TR_RETRY_INTERVAL/HZ);
                } else {
			char **prphase = printphase;
			char **prerror = printerror;
			DPRINTK("TR Adapter misc open failure, error code = ");
			printk("0x%x, Phase: %s, Error: %s\n",
				err, prphase[err/16 -1], prerror[err%16 -1]);
			printk(" retrying after %ds delay...\n",
					TR_RETRY_INTERVAL/HZ);
                }
        } else DPRINTK("open failed: ret_code = %02X..., ", ret_code);
	if (ti->open_action != FAIL) {
		if (ti->open_mode==AUTOMATIC){
			ti->open_action = REOPEN;
			ibmtr_reset_timer(&(ti->tr_timer), dev);
			return;
		}
		wake_up(&ti->wait_for_reset);
		return;
	}
	DPRINTK("FAILURE, CAPUT\n");
}

/******************************************************************************/

static irqreturn_t tok_interrupt(int irq, void *dev_id)
{
	unsigned char status;
	/*  unsigned char status_even ; */
	struct tok_info *ti;
	struct net_device *dev;
#ifdef ENABLE_PAGING
	unsigned char save_srpr;
#endif

	dev = dev_id;
#if TR_VERBOSE
	DPRINTK("Int from tok_driver, dev : %p irq%d\n", dev,irq);
#endif
	ti = netdev_priv(dev);
	if (ti->sram_phys & 1)
		return IRQ_NONE;         /* PCMCIA card extraction flag */
	spin_lock(&(ti->lock));
#ifdef ENABLE_PAGING
	save_srpr = readb(ti->mmio + ACA_OFFSET + ACA_RW + SRPR_EVEN);
#endif

	/* Disable interrupts till processing is finished */
	writeb((~INT_ENABLE), ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_EVEN);

	/* Reset interrupt for ISA boards */
	if (ti->adapter_int_enable)
		outb(0, ti->adapter_int_enable);
	else /* used for PCMCIA cards */
		outb(0, ti->global_int_enable);
        if (ti->do_tok_int == FIRST_INT){
                initial_tok_int(dev);
#ifdef ENABLE_PAGING
                writeb(save_srpr, ti->mmio + ACA_OFFSET + ACA_RW + SRPR_EVEN);
#endif
                spin_unlock(&(ti->lock));
                return IRQ_HANDLED;
        }
	/*  Begin interrupt handler HERE inline to avoid the extra
	    levels of logic and call depth for the original solution. */
	status = readb(ti->mmio + ACA_OFFSET + ACA_RW + ISRP_ODD);
	/*BMSstatus_even = readb (ti->mmio + ACA_OFFSET + ACA_RW + ISRP_EVEN) */
	/*BMSdebugprintk("tok_interrupt: ISRP_ODD = 0x%x ISRP_EVEN = 0x%x\n", */
	/*BMS                                       status,status_even);      */

	if (status & ADAP_CHK_INT) {
		int i;
		void __iomem *check_reason;
		__u8 check_reason_page = 0;
		check_reason = map_address(ti,
			ntohs(readw(ti->mmio+ ACA_OFFSET+ACA_RW + WWCR_EVEN)),
			&check_reason_page);
		SET_PAGE(check_reason_page);

		DPRINTK("Adapter check interrupt\n");
		DPRINTK("8 reason bytes follow: ");
		for (i = 0; i < 8; i++, check_reason++)
			printk("%02X ", (int) readb(check_reason));
		printk("\n");
		writeb(~ADAP_CHK_INT, ti->mmio+ ACA_OFFSET+ACA_RESET+ ISRP_ODD);
		status = readb(ti->mmio + ACA_OFFSET + ACA_RW + ISRA_EVEN);
		DPRINTK("ISRA_EVEN == 0x02%x\n",status);
		ti->open_status = CLOSED;
		ti->sap_status  = CLOSED;
		ti->open_mode   = AUTOMATIC;
		netif_carrier_off(dev);
		netif_stop_queue(dev);
		ti->open_action = RESTART;
		outb(0, dev->base_addr + ADAPTRESET);
		ibmtr_reset_timer(&(ti->tr_timer), dev);/*BMS try to reopen*/
		spin_unlock(&(ti->lock));
		return IRQ_HANDLED;
	}
	if (readb(ti->mmio + ACA_OFFSET + ACA_RW + ISRP_EVEN)
		& (TCR_INT | ERR_INT | ACCESS_INT)) {
		DPRINTK("adapter error: ISRP_EVEN : %02x\n",
			(int)readb(ti->mmio+ ACA_OFFSET + ACA_RW + ISRP_EVEN));
		writeb(~(TCR_INT | ERR_INT | ACCESS_INT),
			ti->mmio + ACA_OFFSET + ACA_RESET + ISRP_EVEN);
		status= readb(ti->mmio+ ACA_OFFSET + ACA_RW + ISRA_EVEN);/*BMS*/
		DPRINTK("ISRA_EVEN == 0x02%x\n",status);/*BMS*/
                writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN);
#ifdef ENABLE_PAGING
                writeb(save_srpr, ti->mmio + ACA_OFFSET + ACA_RW + SRPR_EVEN);
#endif
                spin_unlock(&(ti->lock));
                return IRQ_HANDLED;
        }
	if (status & SRB_RESP_INT) {	/* SRB response */
		SET_PAGE(ti->srb_page);
#if TR_VERBOSE
		DPRINTK("SRB resp: cmd=%02X rsp=%02X\n",
				readb(ti->srb), readb(ti->srb + RETCODE_OFST));
#endif
		switch (readb(ti->srb)) {	/* SRB command check */
		case XMIT_DIR_FRAME:{
			unsigned char xmit_ret_code;
			xmit_ret_code = readb(ti->srb + RETCODE_OFST);
			if (xmit_ret_code == 0xff) break;
			DPRINTK("error on xmit_dir_frame request: %02X\n",
								xmit_ret_code);
			if (ti->current_skb) {
				dev_kfree_skb_irq(ti->current_skb);
				ti->current_skb = NULL;
			}
			/*dev->tbusy = 0;*/
			netif_wake_queue(dev);
			if (ti->readlog_pending)
				ibmtr_readlog(dev);
			break;
		}
		case XMIT_UI_FRAME:{
			unsigned char xmit_ret_code;

			xmit_ret_code = readb(ti->srb + RETCODE_OFST);
			if (xmit_ret_code == 0xff) break;
			DPRINTK("error on xmit_ui_frame request: %02X\n",
								xmit_ret_code);
			if (ti->current_skb) {
				dev_kfree_skb_irq(ti->current_skb);
				ti->current_skb = NULL;
			}
			netif_wake_queue(dev);
			if (ti->readlog_pending)
				ibmtr_readlog(dev);
			break;
		}
		case DIR_OPEN_ADAPTER:
			dir_open_adapter(dev);
			break;
		case DLC_OPEN_SAP:
			if (readb(ti->srb + RETCODE_OFST)) {
				DPRINTK("open_sap failed: ret_code = %02X, "
					"retrying\n",
					(int) readb(ti->srb + RETCODE_OFST));
				ti->open_action = REOPEN;
				ibmtr_reset_timer(&(ti->tr_timer), dev);
				break;
			}
			ti->exsap_station_id = readw(ti->srb + STATION_ID_OFST);
			ti->sap_status = OPEN;/* TR adapter is now available */
			if (ti->open_mode==MANUAL){
				wake_up(&ti->wait_for_reset);
				break;
			}
			netif_wake_queue(dev);
			netif_carrier_on(dev);
			break;
		case DIR_INTERRUPT:
		case DIR_MOD_OPEN_PARAMS:
		case DIR_SET_GRP_ADDR:
		case DIR_SET_FUNC_ADDR:
		case DLC_CLOSE_SAP:
			if (readb(ti->srb + RETCODE_OFST))
				DPRINTK("error on %02X: %02X\n",
					(int) readb(ti->srb + COMMAND_OFST),
					(int) readb(ti->srb + RETCODE_OFST));
			break;
		case DIR_READ_LOG:
			if (readb(ti->srb + RETCODE_OFST)){
				DPRINTK("error on dir_read_log: %02X\n",
					(int) readb(ti->srb + RETCODE_OFST));
				netif_wake_queue(dev);
				break;
			}
#if IBMTR_DEBUG_MESSAGES

#define LINE_ERRORS_OFST                 0
#define INTERNAL_ERRORS_OFST             1
#define BURST_ERRORS_OFST                2
#define AC_ERRORS_OFST                   3
#define ABORT_DELIMITERS_OFST            4
#define LOST_FRAMES_OFST                 6
#define RECV_CONGEST_COUNT_OFST          7
#define FRAME_COPIED_ERRORS_OFST         8
#define FREQUENCY_ERRORS_OFST            9
#define TOKEN_ERRORS_OFST               10

			DPRINTK("Line errors %02X, Internal errors %02X, "
			"Burst errors %02X\n" "A/C errors %02X, "
			"Abort delimiters %02X, Lost frames %02X\n"
			"Receive congestion count %02X, "
			"Frame copied errors %02X\nFrequency errors %02X, "
			"Token errors %02X\n",
			(int) readb(ti->srb + LINE_ERRORS_OFST),
			(int) readb(ti->srb + INTERNAL_ERRORS_OFST),
			(int) readb(ti->srb + BURST_ERRORS_OFST),
			(int) readb(ti->srb + AC_ERRORS_OFST),
			(int) readb(ti->srb + ABORT_DELIMITERS_OFST),
			(int) readb(ti->srb + LOST_FRAMES_OFST),
			(int) readb(ti->srb + RECV_CONGEST_COUNT_OFST),
			(int) readb(ti->srb + FRAME_COPIED_ERRORS_OFST),
			(int) readb(ti->srb + FREQUENCY_ERRORS_OFST),
			(int) readb(ti->srb + TOKEN_ERRORS_OFST));
#endif
			netif_wake_queue(dev);
			break;
		default:
			DPRINTK("Unknown command %02X encountered\n",
						(int) readb(ti->srb));
        	}	/* end switch SRB command check */
		writeb(~SRB_RESP_INT, ti->mmio+ ACA_OFFSET+ACA_RESET+ ISRP_ODD);
	}	/* if SRB response */
	if (status & ASB_FREE_INT) {	/* ASB response */
		SET_PAGE(ti->asb_page);
#if TR_VERBOSE
		DPRINTK("ASB resp: cmd=%02X\n", readb(ti->asb));
#endif

		switch (readb(ti->asb)) {	/* ASB command check */
		case REC_DATA:
		case XMIT_UI_FRAME:
		case XMIT_DIR_FRAME:
			break;
		default:
			DPRINTK("unknown command in asb %02X\n",
						(int) readb(ti->asb));
		}	/* switch ASB command check */
		if (readb(ti->asb + 2) != 0xff)	/* checks ret_code */
			DPRINTK("ASB error %02X in cmd %02X\n",
				(int) readb(ti->asb + 2), (int) readb(ti->asb));
		writeb(~ASB_FREE_INT, ti->mmio+ ACA_OFFSET+ACA_RESET+ ISRP_ODD);
	}	/* if ASB response */

#define STATUS_OFST             6
#define NETW_STATUS_OFST        6

	if (status & ARB_CMD_INT) {	/* ARB response */
		SET_PAGE(ti->arb_page);
#if TR_VERBOSE
		DPRINTK("ARB resp: cmd=%02X\n", readb(ti->arb));
#endif

		switch (readb(ti->arb)) {	/* ARB command check */
		case DLC_STATUS:
			DPRINTK("DLC_STATUS new status: %02X on station %02X\n",
				ntohs(readw(ti->arb + STATUS_OFST)),
				ntohs(readw(ti->arb+ STATION_ID_OFST)));
			break;
		case REC_DATA:
			tr_rx(dev);
			break;
		case RING_STAT_CHANGE:{
			unsigned short ring_status;
			ring_status= ntohs(readw(ti->arb + NETW_STATUS_OFST));
			if (ibmtr_debug_trace & TRC_INIT)
				DPRINTK("Ring Status Change...(0x%x)\n",
								ring_status);
			if(ring_status& (REMOVE_RECV|AUTO_REMOVAL|LOBE_FAULT)){
				netif_stop_queue(dev);
				netif_carrier_off(dev);
				DPRINTK("Remove received, or Auto-removal error"
					", or Lobe fault\n");
				DPRINTK("We'll try to reopen the closed adapter"
					" after a %d second delay.\n",
						TR_RETRY_INTERVAL/HZ);
				/*I was confused: I saw the TR reopening but */
				/*forgot:with an RJ45 in an RJ45/ICS adapter */
				/*but adapter not in the ring, the TR will   */
				/* open, and then soon close and come here.  */
				ti->open_mode = AUTOMATIC;
				ti->open_status = CLOSED; /*12/2000 BMS*/
				ti->open_action = REOPEN;
				ibmtr_reset_timer(&(ti->tr_timer), dev);
			} else if (ring_status & LOG_OVERFLOW) {
				if(netif_queue_stopped(dev))
					ti->readlog_pending = 1;
				else
					ibmtr_readlog(dev);
			}
			break;
          	}
		case XMIT_DATA_REQ:
			tr_tx(dev);
			break;
		default:
			DPRINTK("Unknown command %02X in arb\n",
						(int) readb(ti->arb));
			break;
		}	/* switch ARB command check */
		writeb(~ARB_CMD_INT, ti->mmio+ ACA_OFFSET+ACA_RESET + ISRP_ODD);
		writeb(ARB_FREE, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
	}	/* if ARB response */
	if (status & SSB_RESP_INT) {	/* SSB response */
		unsigned char retcode;
		SET_PAGE(ti->ssb_page);
#if TR_VERBOSE
		DPRINTK("SSB resp: cmd=%02X rsp=%02X\n",
				readb(ti->ssb), readb(ti->ssb + 2));
#endif

		switch (readb(ti->ssb)) {	/* SSB command check */
		case XMIT_DIR_FRAME:
		case XMIT_UI_FRAME:
			retcode = readb(ti->ssb + 2);
			if (retcode && (retcode != 0x22))/* checks ret_code */
				DPRINTK("xmit ret_code: %02X xmit error code: "
					"%02X\n",
					(int)retcode, (int)readb(ti->ssb + 6));
			else
				ti->tr_stats.tx_packets++;
			break;
		case XMIT_XID_CMD:
			DPRINTK("xmit xid ret_code: %02X\n",
						(int) readb(ti->ssb + 2));
		default:
			DPRINTK("Unknown command %02X in ssb\n",
						(int) readb(ti->ssb));
		}	/* SSB command check */
		writeb(~SSB_RESP_INT, ti->mmio+ ACA_OFFSET+ACA_RESET+ ISRP_ODD);
		writeb(SSB_FREE, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
	}	/* if SSB response */
#ifdef ENABLE_PAGING
	writeb(save_srpr, ti->mmio + ACA_OFFSET + ACA_RW + SRPR_EVEN);
#endif
	writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN);
	spin_unlock(&(ti->lock));
	return IRQ_HANDLED;
}				/*tok_interrupt */

/*****************************************************************************/

#define INIT_STATUS_OFST        1
#define INIT_STATUS_2_OFST      2
#define ENCODED_ADDRESS_OFST    8

static void initial_tok_int(struct net_device *dev)
{

	__u32 encoded_addr, hw_encoded_addr;
	struct tok_info *ti;
        unsigned char init_status; /*BMS 12/2000*/

	ti = netdev_priv(dev);

	ti->do_tok_int = NOT_FIRST;

	/* we assign the shared-ram address for ISA devices */
	writeb(ti->sram_base, ti->mmio + ACA_OFFSET + ACA_RW + RRR_EVEN);
#ifndef PCMCIA
        ti->sram_virt = ioremap(((__u32)ti->sram_base << 12), ti->avail_shared_ram);
#endif
	ti->init_srb = map_address(ti,
		ntohs(readw(ti->mmio + ACA_OFFSET + WRBR_EVEN)),
		&ti->init_srb_page);
	if (ti->page_mask && ti->avail_shared_ram == 127) {
		void __iomem *last_512;
		__u8 last_512_page=0;
		int i;
		last_512 = map_address(ti, 0xfe00, &last_512_page);
		/* initialize high section of ram (if necessary) */
		SET_PAGE(last_512_page);
		for (i = 0; i < 512; i++)
			writeb(0, last_512 + i);
	}
	SET_PAGE(ti->init_srb_page);

#if TR_VERBOSE
	{
	int i;

	DPRINTK("ti->init_srb_page=0x%x\n", ti->init_srb_page);
	DPRINTK("init_srb(%p):", ti->init_srb );
	for (i = 0; i < 20; i++)
		printk("%02X ", (int) readb(ti->init_srb + i));
	printk("\n");
	}
#endif

	hw_encoded_addr = readw(ti->init_srb + ENCODED_ADDRESS_OFST);
	encoded_addr = ntohs(hw_encoded_addr);
        init_status= /*BMS 12/2000 check for shallow mode possibility (Turbo)*/
	readb(ti->init_srb+offsetof(struct srb_init_response,init_status));
	/*printk("Initial interrupt: init_status= 0x%02x\n",init_status);*/
	ti->ring_speed = init_status & 0x01 ? 16 : 4;
	DPRINTK("Initial interrupt : %d Mbps, shared RAM base %08x.\n",
				ti->ring_speed, (unsigned int)dev->mem_start);
	ti->auto_speedsave = (readb(ti->init_srb+INIT_STATUS_2_OFST) & 4) != 0;

        if (ti->open_mode == MANUAL)	wake_up(&ti->wait_for_reset);
	else				tok_open_adapter((unsigned long)dev);
        
} /*initial_tok_int() */

/*****************************************************************************/

#define CMD_CORRELATE_OFST      1
#define DHB_ADDRESS_OFST        6

#define FRAME_LENGTH_OFST       6
#define HEADER_LENGTH_OFST      8
#define RSAP_VALUE_OFST         9

static void tr_tx(struct net_device *dev)
{
	struct tok_info *ti = netdev_priv(dev);
	struct trh_hdr *trhdr = (struct trh_hdr *) ti->current_skb->data;
	unsigned int hdr_len;
	__u32 dhb=0,dhb_base;
	void __iomem *dhbuf = NULL;
	unsigned char xmit_command;
	int i,dhb_len=0x4000,src_len,src_offset;
	struct trllc *llc;
	struct srb_xmit xsrb;
	__u8 dhb_page = 0;
	__u8 llc_ssap;

	SET_PAGE(ti->asb_page);

	if (readb(ti->asb+RETCODE_OFST) != 0xFF) DPRINTK("ASB not free !!!\n");

	/* in providing the transmit interrupts, is telling us it is ready for
	   data and providing a shared memory address for us to stuff with data.
	   Here we compute the effective address where we will place data.
	*/
	SET_PAGE(ti->arb_page);
	dhb=dhb_base=ntohs(readw(ti->arb + DHB_ADDRESS_OFST));
	if (ti->page_mask) {
		dhb_page = (dhb_base >> 8) & ti->page_mask;
		dhb=dhb_base & ~(ti->page_mask << 8);
	}
	dhbuf = ti->sram_virt + dhb;

	/* Figure out the size of the 802.5 header */
	if (!(trhdr->saddr[0] & 0x80))	/* RIF present? */
		hdr_len = sizeof(struct trh_hdr) - TR_MAXRIFLEN;
	else
		hdr_len = ((ntohs(trhdr->rcf) & TR_RCF_LEN_MASK) >> 8)
		    + sizeof(struct trh_hdr) - TR_MAXRIFLEN;

	llc = (struct trllc *) (ti->current_skb->data + hdr_len);

	llc_ssap = llc->ssap;
	SET_PAGE(ti->srb_page);
	memcpy_fromio(&xsrb, ti->srb, sizeof(xsrb));
	SET_PAGE(ti->asb_page);
	xmit_command = xsrb.command;

	writeb(xmit_command, ti->asb + COMMAND_OFST);
	writew(xsrb.station_id, ti->asb + STATION_ID_OFST);
	writeb(llc_ssap, ti->asb + RSAP_VALUE_OFST);
	writeb(xsrb.cmd_corr, ti->asb + CMD_CORRELATE_OFST);
	writeb(0, ti->asb + RETCODE_OFST);
	if ((xmit_command == XMIT_XID_CMD) || (xmit_command == XMIT_TEST_CMD)) {
		writew(htons(0x11), ti->asb + FRAME_LENGTH_OFST);
		writeb(0x0e, ti->asb + HEADER_LENGTH_OFST);
		SET_PAGE(dhb_page);
		writeb(AC, dhbuf);
		writeb(LLC_FRAME, dhbuf + 1);
		for (i = 0; i < TR_ALEN; i++)
			writeb((int) 0x0FF, dhbuf + i + 2);
		for (i = 0; i < TR_ALEN; i++)
			writeb(0, dhbuf + i + TR_ALEN + 2);
		writeb(RESP_IN_ASB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
		return;
	}
	/*
	 *    the token ring packet is copied from sk_buff to the adapter
	 *    buffer identified in the command data received with the interrupt.
	 */
	writeb(hdr_len, ti->asb + HEADER_LENGTH_OFST);
	writew(htons(ti->current_skb->len), ti->asb + FRAME_LENGTH_OFST);
	src_len=ti->current_skb->len;
	src_offset=0;
	dhb=dhb_base;
	while(1) {
		if (ti->page_mask) {
			dhb_page=(dhb >> 8) & ti->page_mask;
			dhb=dhb & ~(ti->page_mask << 8);
			dhb_len=0x4000-dhb; /* remaining size of this page */
		}
		dhbuf = ti->sram_virt + dhb;
		SET_PAGE(dhb_page);
		if (src_len > dhb_len) {
			memcpy_toio(dhbuf,&ti->current_skb->data[src_offset],
					dhb_len);
			src_len -= dhb_len;
			src_offset += dhb_len;
			dhb_base+=dhb_len;
			dhb=dhb_base;
			continue;
		}
		memcpy_toio(dhbuf, &ti->current_skb->data[src_offset], src_len);
		break;
	}
	writeb(RESP_IN_ASB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
	ti->tr_stats.tx_bytes += ti->current_skb->len;
	dev_kfree_skb_irq(ti->current_skb);
	ti->current_skb = NULL;
	netif_wake_queue(dev);
	if (ti->readlog_pending)
		ibmtr_readlog(dev);
}				/*tr_tx */

/*****************************************************************************/


#define RECEIVE_BUFFER_OFST     6
#define LAN_HDR_LENGTH_OFST     8
#define DLC_HDR_LENGTH_OFST     9

#define DSAP_OFST               0
#define SSAP_OFST               1
#define LLC_OFST                2
#define PROTID_OFST             3
#define ETHERTYPE_OFST          6

static void tr_rx(struct net_device *dev)
{
	struct tok_info *ti = netdev_priv(dev);
	__u32 rbuffer;
	void __iomem *rbuf, *rbufdata, *llc;
	__u8 rbuffer_page = 0;
	unsigned char *data;
	unsigned int rbuffer_len, lan_hdr_len, hdr_len, ip_len, length;
	unsigned char dlc_hdr_len;
	struct sk_buff *skb;
	unsigned int skb_size = 0;
	int IPv4_p = 0;
	unsigned int chksum = 0;
	struct iphdr *iph;
	struct arb_rec_req rarb;

	SET_PAGE(ti->arb_page);
	memcpy_fromio(&rarb, ti->arb, sizeof(rarb));
	rbuffer = ntohs(rarb.rec_buf_addr) ;
	rbuf = map_address(ti, rbuffer, &rbuffer_page);

	SET_PAGE(ti->asb_page);

	if (readb(ti->asb + RETCODE_OFST) !=0xFF) DPRINTK("ASB not free !!!\n");

	writeb(REC_DATA, ti->asb + COMMAND_OFST);
	writew(rarb.station_id, ti->asb + STATION_ID_OFST);
	writew(rarb.rec_buf_addr, ti->asb + RECEIVE_BUFFER_OFST);

	lan_hdr_len = rarb.lan_hdr_len;
	if (lan_hdr_len > sizeof(struct trh_hdr)) {
		DPRINTK("Linux cannot handle greater than 18 bytes RIF\n");
		return;
	}			/*BMS I added this above just to be very safe */
	dlc_hdr_len = readb(ti->arb + DLC_HDR_LENGTH_OFST);
	hdr_len = lan_hdr_len + sizeof(struct trllc) + sizeof(struct iphdr);

	SET_PAGE(rbuffer_page);
	llc = rbuf + offsetof(struct rec_buf, data) + lan_hdr_len;

#if TR_VERBOSE
	DPRINTK("offsetof data: %02X lan_hdr_len: %02X\n",
	(__u32) offsetof(struct rec_buf, data), (unsigned int) lan_hdr_len);
	DPRINTK("llc: %08X rec_buf_addr: %04X dev->mem_start: %lX\n",
		llc, ntohs(rarb.rec_buf_addr), dev->mem_start);
	DPRINTK("dsap: %02X, ssap: %02X, llc: %02X, protid: %02X%02X%02X, "
		"ethertype: %04X\n",
		(int) readb(llc + DSAP_OFST), (int) readb(llc + SSAP_OFST),
		(int) readb(llc + LLC_OFST), (int) readb(llc + PROTID_OFST),
		(int) readb(llc+PROTID_OFST+1),(int)readb(llc+PROTID_OFST + 2),
		(int) ntohs(readw(llc + ETHERTYPE_OFST)));
#endif
	if (readb(llc + offsetof(struct trllc, llc)) != UI_CMD) {
		SET_PAGE(ti->asb_page);
		writeb(DATA_LOST, ti->asb + RETCODE_OFST);
		ti->tr_stats.rx_dropped++;
		writeb(RESP_IN_ASB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
		return;
	}
	length = ntohs(rarb.frame_len);
	if (readb(llc + DSAP_OFST) == EXTENDED_SAP &&
	   readb(llc + SSAP_OFST) == EXTENDED_SAP &&
		length >= hdr_len)	IPv4_p = 1;
#if TR_VERBOSE
#define SADDR_OFST	8
#define DADDR_OFST	2

	if (!IPv4_p) {

		void __iomem *trhhdr = rbuf + offsetof(struct rec_buf, data);
		u8 saddr[6];
		u8 daddr[6];
		DECLARE_MAC_BUF(mac);
		DECLARE_MAC_BUF(mac2);
		int i;
		for (i = 0 ; i < 6 ; i++)
			saddr[i] = readb(trhhdr + SADDR_OFST + i);
		for (i = 0 ; i < 6 ; i++)
			daddr[i] = readb(trhhdr + DADDR_OFST + i);
		DPRINTK("Probably non-IP frame received.\n");
		DPRINTK("ssap: %02X dsap: %02X "
			"saddr: %s daddr: %$s\n",
			readb(llc + SSAP_OFST), readb(llc + DSAP_OFST),
			print_mac(mac, saddr), print_mac(mac2, daddr));
	}
#endif

	/*BMS handle the case she comes in with few hops but leaves with many */
        skb_size=length-lan_hdr_len+sizeof(struct trh_hdr)+sizeof(struct trllc);

	if (!(skb = dev_alloc_skb(skb_size))) {
		DPRINTK("out of memory. frame dropped.\n");
		ti->tr_stats.rx_dropped++;
		SET_PAGE(ti->asb_page);
		writeb(DATA_LOST, ti->asb + offsetof(struct asb_rec, ret_code));
		writeb(RESP_IN_ASB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);
		return;
	}
	/*BMS again, if she comes in with few but leaves with many */
	skb_reserve(skb, sizeof(struct trh_hdr) - lan_hdr_len);
	skb_put(skb, length);
	data = skb->data;
	rbuffer_len = ntohs(readw(rbuf + offsetof(struct rec_buf, buf_len)));
	rbufdata = rbuf + offsetof(struct rec_buf, data);

	if (IPv4_p) {
		/* Copy the headers without checksumming */
		memcpy_fromio(data, rbufdata, hdr_len);

		/* Watch for padded packets and bogons */
		iph= (struct iphdr *)(data+ lan_hdr_len + sizeof(struct trllc));
		ip_len = ntohs(iph->tot_len) - sizeof(struct iphdr);
		length -= hdr_len;
		if ((ip_len <= length) && (ip_len > 7))
			length = ip_len;
		data += hdr_len;
		rbuffer_len -= hdr_len;
		rbufdata += hdr_len;
	}
	/* Copy the payload... */
#define BUFFER_POINTER_OFST	2
#define BUFFER_LENGTH_OFST      6
	for (;;) {
		if (ibmtr_debug_trace&TRC_INITV && length < rbuffer_len)
			DPRINTK("CURIOUS, length=%d < rbuffer_len=%d\n",
						length,rbuffer_len);
		if (IPv4_p)
			chksum=csum_partial_copy_nocheck((void*)rbufdata,
			    data,length<rbuffer_len?length:rbuffer_len,chksum);
		else
			memcpy_fromio(data, rbufdata, rbuffer_len);
		rbuffer = ntohs(readw(rbuf+BUFFER_POINTER_OFST)) ;
		if (!rbuffer)
			break;
		rbuffer -= 2;
		length -= rbuffer_len;
		data += rbuffer_len;
		rbuf = map_address(ti, rbuffer, &rbuffer_page);
		SET_PAGE(rbuffer_page);
		rbuffer_len = ntohs(readw(rbuf + BUFFER_LENGTH_OFST));
		rbufdata = rbuf + offsetof(struct rec_buf, data);
	}

	SET_PAGE(ti->asb_page);
	writeb(0, ti->asb + offsetof(struct asb_rec, ret_code));

	writeb(RESP_IN_ASB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);

	ti->tr_stats.rx_bytes += skb->len;
	ti->tr_stats.rx_packets++;

	skb->protocol = tr_type_trans(skb, dev);
	if (IPv4_p) {
		skb->csum = chksum;
		skb->ip_summed = CHECKSUM_COMPLETE;
	}
	netif_rx(skb);
	dev->last_rx = jiffies;
}				/*tr_rx */

/*****************************************************************************/

static void ibmtr_reset_timer(struct timer_list *tmr, struct net_device *dev)
{
	tmr->expires = jiffies + TR_RETRY_INTERVAL;
	tmr->data = (unsigned long) dev;
	tmr->function = tok_rerun;
	init_timer(tmr);
	add_timer(tmr);
}

/*****************************************************************************/

void tok_rerun(unsigned long dev_addr){

	struct net_device *dev = (struct net_device *)dev_addr;
	struct tok_info *ti = netdev_priv(dev);

	if ( ti->open_action == RESTART){
		ti->do_tok_int = FIRST_INT;
		outb(0, dev->base_addr + ADAPTRESETREL);
#ifdef ENABLE_PAGING
		if (ti->page_mask)
			writeb(SRPR_ENABLE_PAGING,
				ti->mmio + ACA_OFFSET + ACA_RW + SRPR_EVEN);
#endif

		writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN);
	} else
		tok_open_adapter(dev_addr);
}

/*****************************************************************************/

static void ibmtr_readlog(struct net_device *dev)
{
	struct tok_info *ti;

	ti = netdev_priv(dev);

	ti->readlog_pending = 0;
	SET_PAGE(ti->srb_page);
	writeb(DIR_READ_LOG, ti->srb);
	writeb(INT_ENABLE, ti->mmio + ACA_OFFSET + ACA_SET + ISRP_EVEN);
	writeb(CMD_IN_SRB, ti->mmio + ACA_OFFSET + ACA_SET + ISRA_ODD);

	netif_stop_queue(dev);

}

/*****************************************************************************/

/* tok_get_stats():  Basically a scaffold routine which will return
   the address of the tr_statistics structure associated with
   this device -- the tr.... structure is an ethnet look-alike
   so at least for this iteration may suffice.   */

static struct net_device_stats *tok_get_stats(struct net_device *dev)
{

	struct tok_info *toki;
	toki = netdev_priv(dev);
	return (struct net_device_stats *) &toki->tr_stats;
}

/*****************************************************************************/

static int ibmtr_change_mtu(struct net_device *dev, int mtu)
{
	struct tok_info *ti = netdev_priv(dev);

	if (ti->ring_speed == 16 && mtu > ti->maxmtu16)
		return -EINVAL;
	if (ti->ring_speed == 4 && mtu > ti->maxmtu4)
		return -EINVAL;
	dev->mtu = mtu;
	return 0;
}

/*****************************************************************************/
#ifdef MODULE

/* 3COM 3C619C supports 8 interrupts, 32 I/O ports */
static struct net_device *dev_ibmtr[IBMTR_MAX_ADAPTERS];
static int io[IBMTR_MAX_ADAPTERS] = { 0xa20, 0xa24 };
static int irq[IBMTR_MAX_ADAPTERS];
static int mem[IBMTR_MAX_ADAPTERS];

MODULE_LICENSE("GPL");

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(mem, int, NULL, 0);

static int __init ibmtr_init(void)
{
	int i;
	int count=0;

	find_turbo_adapters(io);

	for (i = 0; io[i] && (i < IBMTR_MAX_ADAPTERS); i++) {
		struct net_device *dev;
		irq[i] = 0;
		mem[i] = 0;
		dev = alloc_trdev(sizeof(struct tok_info));
		if (dev == NULL) { 
			if (i == 0)
				return -ENOMEM;
			break;
		}
		dev->base_addr = io[i];
		dev->irq = irq[i];
		dev->mem_start = mem[i];

		if (ibmtr_probe_card(dev)) {
			free_netdev(dev);
			continue;
		}
		dev_ibmtr[i] = dev;
		count++;
	}
	if (count) return 0;
	printk("ibmtr: register_netdev() returned non-zero.\n");
	return -EIO;
}
module_init(ibmtr_init);

static void __exit ibmtr_cleanup(void)
{
	int i;

	for (i = 0; i < IBMTR_MAX_ADAPTERS; i++){
		if (!dev_ibmtr[i])
			continue;
		unregister_netdev(dev_ibmtr[i]);
		ibmtr_cleanup_card(dev_ibmtr[i]);
		free_netdev(dev_ibmtr[i]);
	}
}
module_exit(ibmtr_cleanup);
#endif
