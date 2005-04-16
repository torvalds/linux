/*
 * Ethernet driver for Motorola MPC8260.
 * Copyright (c) 1999 Dan Malek (dmalek@jlc.net)
 * Copyright (c) 2000 MontaVista Software Inc. (source@mvista.com)
 *	2.3.99 Updates
 *
 * I copied this from the 8xx CPM Ethernet driver, so follow the
 * credits back through that.
 *
 * This version of the driver is somewhat selectable for the different
 * processor/board combinations.  It works for the boards I know about
 * now, and should be easily modified to include others.  Some of the
 * configuration information is contained in <asm/commproc.h> and the
 * remainder is here.
 *
 * Buffer descriptors are kept in the CPM dual port RAM, and the frame
 * buffers are in the host memory.
 *
 * Right now, I am very watseful with the buffers.  I allocate memory
 * pages and then divide them into 2K frame buffers.  This way I know I
 * have buffers large enough to hold one frame within one buffer descriptor.
 * Once I get this working, I will use 64 or 128 byte CPM buffers, which
 * will be much more memory efficient and will easily handle lots of
 * small packets.
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>

#include <asm/immap_cpm2.h>
#include <asm/pgtable.h>
#include <asm/mpc8260.h>
#include <asm/uaccess.h>
#include <asm/cpm2.h>
#include <asm/irq.h>

/*
 *				Theory of Operation
 *
 * The MPC8260 CPM performs the Ethernet processing on an SCC.  It can use
 * an aribtrary number of buffers on byte boundaries, but must have at
 * least two receive buffers to prevent constant overrun conditions.
 *
 * The buffer descriptors are allocated from the CPM dual port memory
 * with the data buffers allocated from host memory, just like all other
 * serial communication protocols.  The host memory buffers are allocated
 * from the free page pool, and then divided into smaller receive and
 * transmit buffers.  The size of the buffers should be a power of two,
 * since that nicely divides the page.  This creates a ring buffer
 * structure similar to the LANCE and other controllers.
 *
 * Like the LANCE driver:
 * The driver runs as two independent, single-threaded flows of control.  One
 * is the send-packet routine, which enforces single-threaded use by the
 * cep->tx_busy flag.  The other thread is the interrupt handler, which is
 * single threaded by the hardware and other software.
 */

/* The transmitter timeout
 */
#define TX_TIMEOUT	(2*HZ)

/* The number of Tx and Rx buffers.  These are allocated from the page
 * pool.  The code may assume these are power of two, so it is best
 * to keep them that size.
 * We don't need to allocate pages for the transmitter.  We just use
 * the skbuffer directly.
 */
#define CPM_ENET_RX_PAGES	4
#define CPM_ENET_RX_FRSIZE	2048
#define CPM_ENET_RX_FRPPG	(PAGE_SIZE / CPM_ENET_RX_FRSIZE)
#define RX_RING_SIZE		(CPM_ENET_RX_FRPPG * CPM_ENET_RX_PAGES)
#define TX_RING_SIZE		8	/* Must be power of two */
#define TX_RING_MOD_MASK	7	/*   for this to work */

/* The CPM stores dest/src/type, data, and checksum for receive packets.
 */
#define PKT_MAXBUF_SIZE		1518
#define PKT_MINBUF_SIZE		64
#define PKT_MAXBLR_SIZE		1520

/* The CPM buffer descriptors track the ring buffers.  The rx_bd_base and
 * tx_bd_base always point to the base of the buffer descriptors.  The
 * cur_rx and cur_tx point to the currently available buffer.
 * The dirty_tx tracks the current buffer that is being sent by the
 * controller.  The cur_tx and dirty_tx are equal under both completely
 * empty and completely full conditions.  The empty/ready indicator in
 * the buffer descriptor determines the actual condition.
 */
struct scc_enet_private {
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct	sk_buff* tx_skbuff[TX_RING_SIZE];
	ushort	skb_cur;
	ushort	skb_dirty;

	/* CPM dual port RAM relative addresses.
	*/
	cbd_t	*rx_bd_base;		/* Address of Rx and Tx buffers. */
	cbd_t	*tx_bd_base;
	cbd_t	*cur_rx, *cur_tx;		/* The next free ring entry */
	cbd_t	*dirty_tx;	/* The ring entries to be free()ed. */
	scc_t	*sccp;
	struct	net_device_stats stats;
	uint	tx_full;
	spinlock_t lock;
};

static int scc_enet_open(struct net_device *dev);
static int scc_enet_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int scc_enet_rx(struct net_device *dev);
static irqreturn_t scc_enet_interrupt(int irq, void *dev_id, struct pt_regs *);
static int scc_enet_close(struct net_device *dev);
static struct net_device_stats *scc_enet_get_stats(struct net_device *dev);
static void set_multicast_list(struct net_device *dev);

/* These will be configurable for the SCC choice.
*/
#define CPM_ENET_BLOCK	CPM_CR_SCC1_SBLOCK
#define CPM_ENET_PAGE	CPM_CR_SCC1_PAGE
#define PROFF_ENET	PROFF_SCC1
#define SCC_ENET	0
#define SIU_INT_ENET	SIU_INT_SCC1

/* These are both board and SCC dependent....
*/
#define PD_ENET_RXD	((uint)0x00000001)
#define PD_ENET_TXD	((uint)0x00000002)
#define PD_ENET_TENA	((uint)0x00000004)
#define PC_ENET_RENA	((uint)0x00020000)
#define PC_ENET_CLSN	((uint)0x00000004)
#define PC_ENET_TXCLK	((uint)0x00000800)
#define PC_ENET_RXCLK	((uint)0x00000400)
#define CMX_CLK_ROUTE	((uint)0x25000000)
#define CMX_CLK_MASK	((uint)0xff000000)

/* Specific to a board.
*/
#define PC_EST8260_ENET_LOOPBACK	((uint)0x80000000)
#define PC_EST8260_ENET_SQE		((uint)0x40000000)
#define PC_EST8260_ENET_NOTFD		((uint)0x20000000)

static int
scc_enet_open(struct net_device *dev)
{

	/* I should reset the ring buffers here, but I don't yet know
	 * a simple way to do that.
	 */
	netif_start_queue(dev);
	return 0;					/* Always succeed */
}

static int
scc_enet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct scc_enet_private *cep = (struct scc_enet_private *)dev->priv;
	volatile cbd_t	*bdp;


	/* Fill in a Tx ring entry */
	bdp = cep->cur_tx;

#ifndef final_version
	if (bdp->cbd_sc & BD_ENET_TX_READY) {
		/* Ooops.  All transmit buffers are full.  Bail out.
		 * This should not happen, since cep->tx_full should be set.
		 */
		printk("%s: tx queue full!.\n", dev->name);
		return 1;
	}
#endif

	/* Clear all of the status flags.
	 */
	bdp->cbd_sc &= ~BD_ENET_TX_STATS;

	/* If the frame is short, tell CPM to pad it.
	*/
	if (skb->len <= ETH_ZLEN)
		bdp->cbd_sc |= BD_ENET_TX_PAD;
	else
		bdp->cbd_sc &= ~BD_ENET_TX_PAD;

	/* Set buffer length and buffer pointer.
	*/
	bdp->cbd_datlen = skb->len;
	bdp->cbd_bufaddr = __pa(skb->data);

	/* Save skb pointer.
	*/
	cep->tx_skbuff[cep->skb_cur] = skb;

	cep->stats.tx_bytes += skb->len;
	cep->skb_cur = (cep->skb_cur+1) & TX_RING_MOD_MASK;

	spin_lock_irq(&cep->lock);

	/* Send it on its way.  Tell CPM its ready, interrupt when done,
	 * its the last BD of the frame, and to put the CRC on the end.
	 */
	bdp->cbd_sc |= (BD_ENET_TX_READY | BD_ENET_TX_INTR | BD_ENET_TX_LAST | BD_ENET_TX_TC);

	dev->trans_start = jiffies;

	/* If this was the last BD in the ring, start at the beginning again.
	*/
	if (bdp->cbd_sc & BD_ENET_TX_WRAP)
		bdp = cep->tx_bd_base;
	else
		bdp++;

	if (bdp->cbd_sc & BD_ENET_TX_READY) {
		netif_stop_queue(dev);
		cep->tx_full = 1;
	}

	cep->cur_tx = (cbd_t *)bdp;

	spin_unlock_irq(&cep->lock);

	return 0;
}

static void
scc_enet_timeout(struct net_device *dev)
{
	struct scc_enet_private *cep = (struct scc_enet_private *)dev->priv;

	printk("%s: transmit timed out.\n", dev->name);
	cep->stats.tx_errors++;
#ifndef final_version
	{
		int	i;
		cbd_t	*bdp;
		printk(" Ring data dump: cur_tx %p%s cur_rx %p.\n",
		       cep->cur_tx, cep->tx_full ? " (full)" : "",
		       cep->cur_rx);
		bdp = cep->tx_bd_base;
		printk(" Tx @base %p :\n", bdp);
		for (i = 0 ; i < TX_RING_SIZE; i++, bdp++)
			printk("%04x %04x %08x\n",
			       bdp->cbd_sc,
			       bdp->cbd_datlen,
			       bdp->cbd_bufaddr);
		bdp = cep->rx_bd_base;
		printk(" Rx @base %p :\n", bdp);
		for (i = 0 ; i < RX_RING_SIZE; i++, bdp++)
			printk("%04x %04x %08x\n",
			       bdp->cbd_sc,
			       bdp->cbd_datlen,
			       bdp->cbd_bufaddr);
	}
#endif
	if (!cep->tx_full)
		netif_wake_queue(dev);
}

/* The interrupt handler.
 * This is called from the CPM handler, not the MPC core interrupt.
 */
static irqreturn_t
scc_enet_interrupt(int irq, void * dev_id, struct pt_regs * regs)
{
	struct	net_device *dev = dev_id;
	volatile struct	scc_enet_private *cep;
	volatile cbd_t	*bdp;
	ushort	int_events;
	int	must_restart;

	cep = (struct scc_enet_private *)dev->priv;

	/* Get the interrupt events that caused us to be here.
	*/
	int_events = cep->sccp->scc_scce;
	cep->sccp->scc_scce = int_events;
	must_restart = 0;

	/* Handle receive event in its own function.
	*/
	if (int_events & SCCE_ENET_RXF)
		scc_enet_rx(dev_id);

	/* Check for a transmit error.  The manual is a little unclear
	 * about this, so the debug code until I get it figured out.  It
	 * appears that if TXE is set, then TXB is not set.  However,
	 * if carrier sense is lost during frame transmission, the TXE
	 * bit is set, "and continues the buffer transmission normally."
	 * I don't know if "normally" implies TXB is set when the buffer
	 * descriptor is closed.....trial and error :-).
	 */

	/* Transmit OK, or non-fatal error.  Update the buffer descriptors.
	*/
	if (int_events & (SCCE_ENET_TXE | SCCE_ENET_TXB)) {
	    spin_lock(&cep->lock);
	    bdp = cep->dirty_tx;
	    while ((bdp->cbd_sc&BD_ENET_TX_READY)==0) {
		if ((bdp==cep->cur_tx) && (cep->tx_full == 0))
		    break;

		if (bdp->cbd_sc & BD_ENET_TX_HB)	/* No heartbeat */
			cep->stats.tx_heartbeat_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_LC)	/* Late collision */
			cep->stats.tx_window_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_RL)	/* Retrans limit */
			cep->stats.tx_aborted_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_UN)	/* Underrun */
			cep->stats.tx_fifo_errors++;
		if (bdp->cbd_sc & BD_ENET_TX_CSL)	/* Carrier lost */
			cep->stats.tx_carrier_errors++;


		/* No heartbeat or Lost carrier are not really bad errors.
		 * The others require a restart transmit command.
		 */
		if (bdp->cbd_sc &
		    (BD_ENET_TX_LC | BD_ENET_TX_RL | BD_ENET_TX_UN)) {
			must_restart = 1;
			cep->stats.tx_errors++;
		}

		cep->stats.tx_packets++;

		/* Deferred means some collisions occurred during transmit,
		 * but we eventually sent the packet OK.
		 */
		if (bdp->cbd_sc & BD_ENET_TX_DEF)
			cep->stats.collisions++;

		/* Free the sk buffer associated with this last transmit.
		*/
		dev_kfree_skb_irq(cep->tx_skbuff[cep->skb_dirty]);
		cep->skb_dirty = (cep->skb_dirty + 1) & TX_RING_MOD_MASK;

		/* Update pointer to next buffer descriptor to be transmitted.
		*/
		if (bdp->cbd_sc & BD_ENET_TX_WRAP)
			bdp = cep->tx_bd_base;
		else
			bdp++;

		/* I don't know if we can be held off from processing these
		 * interrupts for more than one frame time.  I really hope
		 * not.  In such a case, we would now want to check the
		 * currently available BD (cur_tx) and determine if any
		 * buffers between the dirty_tx and cur_tx have also been
		 * sent.  We would want to process anything in between that
		 * does not have BD_ENET_TX_READY set.
		 */

		/* Since we have freed up a buffer, the ring is no longer
		 * full.
		 */
		if (cep->tx_full) {
			cep->tx_full = 0;
			if (netif_queue_stopped(dev)) {
				netif_wake_queue(dev);
			}
		}

		cep->dirty_tx = (cbd_t *)bdp;
	    }

	    if (must_restart) {
		volatile cpm_cpm2_t *cp;

		/* Some transmit errors cause the transmitter to shut
		 * down.  We now issue a restart transmit.  Since the
		 * errors close the BD and update the pointers, the restart
		 * _should_ pick up without having to reset any of our
		 * pointers either.
		 */

		cp = cpmp;
		cp->cp_cpcr =
		    mk_cr_cmd(CPM_ENET_PAGE, CPM_ENET_BLOCK, 0,
		    			CPM_CR_RESTART_TX) | CPM_CR_FLG;
		while (cp->cp_cpcr & CPM_CR_FLG);
	    }
	    spin_unlock(&cep->lock);
	}

	/* Check for receive busy, i.e. packets coming but no place to
	 * put them.  This "can't happen" because the receive interrupt
	 * is tossing previous frames.
	 */
	if (int_events & SCCE_ENET_BSY) {
		cep->stats.rx_dropped++;
		printk("SCC ENET: BSY can't happen.\n");
	}

	return IRQ_HANDLED;
}

/* During a receive, the cur_rx points to the current incoming buffer.
 * When we update through the ring, if the next incoming buffer has
 * not been given to the system, we just set the empty indicator,
 * effectively tossing the packet.
 */
static int
scc_enet_rx(struct net_device *dev)
{
	struct	scc_enet_private *cep;
	volatile cbd_t	*bdp;
	struct	sk_buff *skb;
	ushort	pkt_len;

	cep = (struct scc_enet_private *)dev->priv;

	/* First, grab all of the stats for the incoming packet.
	 * These get messed up if we get called due to a busy condition.
	 */
	bdp = cep->cur_rx;

for (;;) {
	if (bdp->cbd_sc & BD_ENET_RX_EMPTY)
		break;

#ifndef final_version
	/* Since we have allocated space to hold a complete frame, both
	 * the first and last indicators should be set.
	 */
	if ((bdp->cbd_sc & (BD_ENET_RX_FIRST | BD_ENET_RX_LAST)) !=
		(BD_ENET_RX_FIRST | BD_ENET_RX_LAST))
			printk("CPM ENET: rcv is not first+last\n");
#endif

	/* Frame too long or too short.
	*/
	if (bdp->cbd_sc & (BD_ENET_RX_LG | BD_ENET_RX_SH))
		cep->stats.rx_length_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_NO)	/* Frame alignment */
		cep->stats.rx_frame_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_CR)	/* CRC Error */
		cep->stats.rx_crc_errors++;
	if (bdp->cbd_sc & BD_ENET_RX_OV)	/* FIFO overrun */
		cep->stats.rx_crc_errors++;

	/* Report late collisions as a frame error.
	 * On this error, the BD is closed, but we don't know what we
	 * have in the buffer.  So, just drop this frame on the floor.
	 */
	if (bdp->cbd_sc & BD_ENET_RX_CL) {
		cep->stats.rx_frame_errors++;
	}
	else {

		/* Process the incoming frame.
		*/
		cep->stats.rx_packets++;
		pkt_len = bdp->cbd_datlen;
		cep->stats.rx_bytes += pkt_len;

		/* This does 16 byte alignment, much more than we need.
		 * The packet length includes FCS, but we don't want to
		 * include that when passing upstream as it messes up
		 * bridging applications.
		 */
		skb = dev_alloc_skb(pkt_len-4);

		if (skb == NULL) {
			printk("%s: Memory squeeze, dropping packet.\n", dev->name);
			cep->stats.rx_dropped++;
		}
		else {
			skb->dev = dev;
			skb_put(skb,pkt_len-4);	/* Make room */
			eth_copy_and_sum(skb,
				(unsigned char *)__va(bdp->cbd_bufaddr),
				pkt_len-4, 0);
			skb->protocol=eth_type_trans(skb,dev);
			netif_rx(skb);
		}
	}

	/* Clear the status flags for this buffer.
	*/
	bdp->cbd_sc &= ~BD_ENET_RX_STATS;

	/* Mark the buffer empty.
	*/
	bdp->cbd_sc |= BD_ENET_RX_EMPTY;

	/* Update BD pointer to next entry.
	*/
	if (bdp->cbd_sc & BD_ENET_RX_WRAP)
		bdp = cep->rx_bd_base;
	else
		bdp++;

   }
	cep->cur_rx = (cbd_t *)bdp;

	return 0;
}

static int
scc_enet_close(struct net_device *dev)
{
	/* Don't know what to do yet.
	*/
	netif_stop_queue(dev);

	return 0;
}

static struct net_device_stats *scc_enet_get_stats(struct net_device *dev)
{
	struct scc_enet_private *cep = (struct scc_enet_private *)dev->priv;

	return &cep->stats;
}

/* Set or clear the multicast filter for this adaptor.
 * Skeleton taken from sunlance driver.
 * The CPM Ethernet implementation allows Multicast as well as individual
 * MAC address filtering.  Some of the drivers check to make sure it is
 * a group multicast address, and discard those that are not.  I guess I
 * will do the same for now, but just remove the test if you want
 * individual filtering as well (do the upper net layers want or support
 * this kind of feature?).
 */

static void set_multicast_list(struct net_device *dev)
{
	struct	scc_enet_private *cep;
	struct	dev_mc_list *dmi;
	u_char	*mcptr, *tdptr;
	volatile scc_enet_t *ep;
	int	i, j;
	cep = (struct scc_enet_private *)dev->priv;

	/* Get pointer to SCC area in parameter RAM.
	*/
	ep = (scc_enet_t *)dev->base_addr;

	if (dev->flags&IFF_PROMISC) {
	
		/* Log any net taps. */
		printk("%s: Promiscuous mode enabled.\n", dev->name);
		cep->sccp->scc_psmr |= SCC_PSMR_PRO;
	} else {

		cep->sccp->scc_psmr &= ~SCC_PSMR_PRO;

		if (dev->flags & IFF_ALLMULTI) {
			/* Catch all multicast addresses, so set the
			 * filter to all 1's.
			 */
			ep->sen_gaddr1 = 0xffff;
			ep->sen_gaddr2 = 0xffff;
			ep->sen_gaddr3 = 0xffff;
			ep->sen_gaddr4 = 0xffff;
		}
		else {
			/* Clear filter and add the addresses in the list.
			*/
			ep->sen_gaddr1 = 0;
			ep->sen_gaddr2 = 0;
			ep->sen_gaddr3 = 0;
			ep->sen_gaddr4 = 0;

			dmi = dev->mc_list;

			for (i=0; i<dev->mc_count; i++) {
		
				/* Only support group multicast for now.
				*/
				if (!(dmi->dmi_addr[0] & 1))
					continue;

				/* The address in dmi_addr is LSB first,
				 * and taddr is MSB first.  We have to
				 * copy bytes MSB first from dmi_addr.
				 */
				mcptr = (u_char *)dmi->dmi_addr + 5;
				tdptr = (u_char *)&ep->sen_taddrh;
				for (j=0; j<6; j++)
					*tdptr++ = *mcptr--;

				/* Ask CPM to run CRC and set bit in
				 * filter mask.
				 */
				cpmp->cp_cpcr = mk_cr_cmd(CPM_ENET_PAGE,
						CPM_ENET_BLOCK, 0,
						CPM_CR_SET_GADDR) | CPM_CR_FLG;
				/* this delay is necessary here -- Cort */
				udelay(10);
				while (cpmp->cp_cpcr & CPM_CR_FLG);
			}
		}
	}
}

/* Initialize the CPM Ethernet on SCC.
 */
static int __init scc_enet_init(void)
{
	struct net_device *dev;
	struct scc_enet_private *cep;
	int i, j, err;
	uint dp_offset;
	unsigned char	*eap;
	unsigned long	mem_addr;
	bd_t		*bd;
	volatile	cbd_t		*bdp;
	volatile	cpm_cpm2_t	*cp;
	volatile	scc_t		*sccp;
	volatile	scc_enet_t	*ep;
	volatile	cpm2_map_t		*immap;
	volatile	iop_cpm2_t	*io;

	cp = cpmp;	/* Get pointer to Communication Processor */

	immap = (cpm2_map_t *)CPM_MAP_ADDR;	/* and to internal registers */
	io = &immap->im_ioport;

	bd = (bd_t *)__res;

	/* Create an Ethernet device instance.
	*/
	dev = alloc_etherdev(sizeof(*cep));
	if (!dev)
		return -ENOMEM;

	cep = dev->priv;
	spin_lock_init(&cep->lock);

	/* Get pointer to SCC area in parameter RAM.
	*/
	ep = (scc_enet_t *)(&immap->im_dprambase[PROFF_ENET]);

	/* And another to the SCC register area.
	*/
	sccp = (volatile scc_t *)(&immap->im_scc[SCC_ENET]);
	cep->sccp = (scc_t *)sccp;		/* Keep the pointer handy */

	/* Disable receive and transmit in case someone left it running.
	*/
	sccp->scc_gsmrl &= ~(SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	/* Configure port C and D pins for SCC Ethernet.  This
	 * won't work for all SCC possibilities....it will be
	 * board/port specific.
	 */
	io->iop_pparc |=
		(PC_ENET_RENA | PC_ENET_CLSN | PC_ENET_TXCLK | PC_ENET_RXCLK);
	io->iop_pdirc &=
		~(PC_ENET_RENA | PC_ENET_CLSN | PC_ENET_TXCLK | PC_ENET_RXCLK);
	io->iop_psorc &=
		~(PC_ENET_RENA | PC_ENET_TXCLK | PC_ENET_RXCLK);
	io->iop_psorc |= PC_ENET_CLSN;

	io->iop_ppard |= (PD_ENET_RXD | PD_ENET_TXD | PD_ENET_TENA);
	io->iop_pdird |= (PD_ENET_TXD | PD_ENET_TENA);
	io->iop_pdird &= ~PD_ENET_RXD;
	io->iop_psord |= PD_ENET_TXD;
	io->iop_psord &= ~(PD_ENET_RXD | PD_ENET_TENA);

	/* Configure Serial Interface clock routing.
	 * First, clear all SCC bits to zero, then set the ones we want.
	 */
	immap->im_cpmux.cmx_scr &= ~CMX_CLK_MASK;
	immap->im_cpmux.cmx_scr |= CMX_CLK_ROUTE;

	/* Allocate space for the buffer descriptors in the DP ram.
	 * These are relative offsets in the DP ram address space.
	 * Initialize base addresses for the buffer descriptors.
	 */
	dp_offset = cpm_dpalloc(sizeof(cbd_t) * RX_RING_SIZE, 8);
	ep->sen_genscc.scc_rbase = dp_offset;
	cep->rx_bd_base = (cbd_t *)cpm_dpram_addr(dp_offset);

	dp_offset = cpm_dpalloc(sizeof(cbd_t) * TX_RING_SIZE, 8);
	ep->sen_genscc.scc_tbase = dp_offset;
	cep->tx_bd_base = (cbd_t *)cpm_dpram_addr(dp_offset);

	cep->dirty_tx = cep->cur_tx = cep->tx_bd_base;
	cep->cur_rx = cep->rx_bd_base;

	ep->sen_genscc.scc_rfcr = CPMFCR_GBL | CPMFCR_EB;
	ep->sen_genscc.scc_tfcr = CPMFCR_GBL | CPMFCR_EB;

	/* Set maximum bytes per receive buffer.
	 * This appears to be an Ethernet frame size, not the buffer
	 * fragment size.  It must be a multiple of four.
	 */
	ep->sen_genscc.scc_mrblr = PKT_MAXBLR_SIZE;

	/* Set CRC preset and mask.
	*/
	ep->sen_cpres = 0xffffffff;
	ep->sen_cmask = 0xdebb20e3;

	ep->sen_crcec = 0;	/* CRC Error counter */
	ep->sen_alec = 0;	/* alignment error counter */
	ep->sen_disfc = 0;	/* discard frame counter */

	ep->sen_pads = 0x8888;	/* Tx short frame pad character */
	ep->sen_retlim = 15;	/* Retry limit threshold */

	ep->sen_maxflr = PKT_MAXBUF_SIZE;   /* maximum frame length register */
	ep->sen_minflr = PKT_MINBUF_SIZE;  /* minimum frame length register */

	ep->sen_maxd1 = PKT_MAXBLR_SIZE;	/* maximum DMA1 length */
	ep->sen_maxd2 = PKT_MAXBLR_SIZE;	/* maximum DMA2 length */

	/* Clear hash tables.
	*/
	ep->sen_gaddr1 = 0;
	ep->sen_gaddr2 = 0;
	ep->sen_gaddr3 = 0;
	ep->sen_gaddr4 = 0;
	ep->sen_iaddr1 = 0;
	ep->sen_iaddr2 = 0;
	ep->sen_iaddr3 = 0;
	ep->sen_iaddr4 = 0;

	/* Set Ethernet station address.
	 *
	 * This is supplied in the board information structure, so we
	 * copy that into the controller.
	 */
	eap = (unsigned char *)&(ep->sen_paddrh);
	for (i=5; i>=0; i--)
		*eap++ = dev->dev_addr[i] = bd->bi_enetaddr[i];

	ep->sen_pper = 0;	/* 'cause the book says so */
	ep->sen_taddrl = 0;	/* temp address (LSB) */
	ep->sen_taddrm = 0;
	ep->sen_taddrh = 0;	/* temp address (MSB) */

	/* Now allocate the host memory pages and initialize the
	 * buffer descriptors.
	 */
	bdp = cep->tx_bd_base;
	for (i=0; i<TX_RING_SIZE; i++) {

		/* Initialize the BD for every fragment in the page.
		*/
		bdp->cbd_sc = 0;
		bdp->cbd_bufaddr = 0;
		bdp++;
	}

	/* Set the last buffer to wrap.
	*/
	bdp--;
	bdp->cbd_sc |= BD_SC_WRAP;

	bdp = cep->rx_bd_base;
	for (i=0; i<CPM_ENET_RX_PAGES; i++) {

		/* Allocate a page.
		*/
		mem_addr = __get_free_page(GFP_KERNEL);
		/* BUG: no check for failure */

		/* Initialize the BD for every fragment in the page.
		*/
		for (j=0; j<CPM_ENET_RX_FRPPG; j++) {
			bdp->cbd_sc = BD_ENET_RX_EMPTY | BD_ENET_RX_INTR;
			bdp->cbd_bufaddr = __pa(mem_addr);
			mem_addr += CPM_ENET_RX_FRSIZE;
			bdp++;
		}
	}

	/* Set the last buffer to wrap.
	*/
	bdp--;
	bdp->cbd_sc |= BD_SC_WRAP;

	/* Let's re-initialize the channel now.  We have to do it later
	 * than the manual describes because we have just now finished
	 * the BD initialization.
	 */
	cpmp->cp_cpcr = mk_cr_cmd(CPM_ENET_PAGE, CPM_ENET_BLOCK, 0,
			CPM_CR_INIT_TRX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	cep->skb_cur = cep->skb_dirty = 0;

	sccp->scc_scce = 0xffff;	/* Clear any pending events */

	/* Enable interrupts for transmit error, complete frame
	 * received, and any transmit buffer we have also set the
	 * interrupt flag.
	 */
	sccp->scc_sccm = (SCCE_ENET_TXE | SCCE_ENET_RXF | SCCE_ENET_TXB);

	/* Install our interrupt handler.
	*/
	request_irq(SIU_INT_ENET, scc_enet_interrupt, 0, "enet", dev);
	/* BUG: no check for failure */

	/* Set GSMR_H to enable all normal operating modes.
	 * Set GSMR_L to enable Ethernet to MC68160.
	 */
	sccp->scc_gsmrh = 0;
	sccp->scc_gsmrl = (SCC_GSMRL_TCI | SCC_GSMRL_TPL_48 | SCC_GSMRL_TPP_10 | SCC_GSMRL_MODE_ENET);

	/* Set sync/delimiters.
	*/
	sccp->scc_dsr = 0xd555;

	/* Set processing mode.  Use Ethernet CRC, catch broadcast, and
	 * start frame search 22 bit times after RENA.
	 */
	sccp->scc_psmr = (SCC_PSMR_ENCRC | SCC_PSMR_NIB22);

	/* It is now OK to enable the Ethernet transmitter.
	 * Unfortunately, there are board implementation differences here.
	 */
	io->iop_pparc &= ~(PC_EST8260_ENET_LOOPBACK |
				PC_EST8260_ENET_SQE | PC_EST8260_ENET_NOTFD);
	io->iop_psorc &= ~(PC_EST8260_ENET_LOOPBACK |
				PC_EST8260_ENET_SQE | PC_EST8260_ENET_NOTFD);
	io->iop_pdirc |= (PC_EST8260_ENET_LOOPBACK |
				PC_EST8260_ENET_SQE | PC_EST8260_ENET_NOTFD);
	io->iop_pdatc &= ~(PC_EST8260_ENET_LOOPBACK | PC_EST8260_ENET_SQE);
	io->iop_pdatc |= PC_EST8260_ENET_NOTFD;

	dev->base_addr = (unsigned long)ep;

	/* The CPM Ethernet specific entries in the device structure. */
	dev->open = scc_enet_open;
	dev->hard_start_xmit = scc_enet_start_xmit;
	dev->tx_timeout = scc_enet_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->stop = scc_enet_close;
	dev->get_stats = scc_enet_get_stats;
	dev->set_multicast_list = set_multicast_list;

	/* And last, enable the transmit and receive processing.
	*/
	sccp->scc_gsmrl |= (SCC_GSMRL_ENR | SCC_GSMRL_ENT);

	err = register_netdev(dev);
	if (err) {
		free_netdev(dev);
		return err;
	}

	printk("%s: SCC ENET Version 0.1, ", dev->name);
	for (i=0; i<5; i++)
		printk("%02x:", dev->dev_addr[i]);
	printk("%02x\n", dev->dev_addr[5]);

	return 0;
}

module_init(scc_enet_init);
