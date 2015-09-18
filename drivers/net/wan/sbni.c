/* sbni.c:  Granch SBNI12 leased line adapters driver for linux
 *
 *	Written 2001 by Denis I.Timofeev (timofeev@granch.ru)
 *
 *	Previous versions were written by Yaroslav Polyakov,
 *	Alexey Zverev and Max Khon.
 *
 *	Driver supports SBNI12-02,-04,-05,-10,-11 cards, single and
 *	double-channel, PCI and ISA modifications.
 *	More info and useful utilities to work with SBNI12 cards you can find
 *	at http://www.granch.com (English) or http://www.granch.ru (Russian)
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License.
 *
 *
 *  5.0.1	Jun 22 2001
 *	  - Fixed bug in probe
 *  5.0.0	Jun 06 2001
 *	  - Driver was completely redesigned by Denis I.Timofeev,
 *	  - now PCI/Dual, ISA/Dual (with single interrupt line) models are
 *	  - supported
 *  3.3.0	Thu Feb 24 21:30:28 NOVT 2000 
 *        - PCI cards support
 *  3.2.0	Mon Dec 13 22:26:53 NOVT 1999
 * 	  - Completely rebuilt all the packet storage system
 * 	  -    to work in Ethernet-like style.
 *  3.1.1	just fixed some bugs (5 aug 1999)
 *  3.1.0	added balancing feature	(26 apr 1999)
 *  3.0.1	just fixed some bugs (14 apr 1999).
 *  3.0.0	Initial Revision, Yaroslav Polyakov (24 Feb 1999)
 *        - added pre-calculation for CRC, fixed bug with "len-2" frames, 
 *        - removed outbound fragmentation (MTU=1000), written CRC-calculation 
 *        - on asm, added work with hard_headers and now we have our own cache 
 *        - for them, optionally supported word-interchange on some chipsets,
 * 
 *	Known problem: this driver wasn't tested on multiprocessor machine.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/fcntl.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <net/net_namespace.h>
#include <net/arp.h>
#include <net/Space.h>

#include <asm/io.h>
#include <asm/types.h>
#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "sbni.h"

/* device private data */

struct net_local {
	struct timer_list	watchdog;

	spinlock_t	lock;
	struct sk_buff  *rx_buf_p;		/* receive buffer ptr */
	struct sk_buff  *tx_buf_p;		/* transmit buffer ptr */
	
	unsigned int	framelen;		/* current frame length */
	unsigned int	maxframe;		/* maximum valid frame length */
	unsigned int	state;
	unsigned int	inppos, outpos;		/* positions in rx/tx buffers */

	/* transmitting frame number - from frames qty to 1 */
	unsigned int	tx_frameno;

	/* expected number of next receiving frame */
	unsigned int	wait_frameno;

	/* count of failed attempts to frame send - 32 attempts do before
	   error - while receiver tunes on opposite side of wire */
	unsigned int	trans_errors;

	/* idle time; send pong when limit exceeded */
	unsigned int	timer_ticks;

	/* fields used for receive level autoselection */
	int	delta_rxl;
	unsigned int	cur_rxl_index, timeout_rxl;
	unsigned long	cur_rxl_rcvd, prev_rxl_rcvd;

	struct sbni_csr1	csr1;		/* current value of CSR1 */
	struct sbni_in_stats	in_stats; 	/* internal statistics */ 

	struct net_device		*second;	/* for ISA/dual cards */

#ifdef CONFIG_SBNI_MULTILINE
	struct net_device		*master;
	struct net_device		*link;
#endif
};


static int  sbni_card_probe( unsigned long );
static int  sbni_pci_probe( struct net_device  * );
static struct net_device  *sbni_probe1(struct net_device *, unsigned long, int);
static int  sbni_open( struct net_device * );
static int  sbni_close( struct net_device * );
static netdev_tx_t sbni_start_xmit(struct sk_buff *,
					 struct net_device * );
static int  sbni_ioctl( struct net_device *, struct ifreq *, int );
static void  set_multicast_list( struct net_device * );

static irqreturn_t sbni_interrupt( int, void * );
static void  handle_channel( struct net_device * );
static int   recv_frame( struct net_device * );
static void  send_frame( struct net_device * );
static int   upload_data( struct net_device *,
			  unsigned, unsigned, unsigned, u32 );
static void  download_data( struct net_device *, u32 * );
static void  sbni_watchdog( unsigned long );
static void  interpret_ack( struct net_device *, unsigned );
static int   append_frame_to_pkt( struct net_device *, unsigned, u32 );
static void  indicate_pkt( struct net_device * );
static void  card_start( struct net_device * );
static void  prepare_to_send( struct sk_buff *, struct net_device * );
static void  drop_xmit_queue( struct net_device * );
static void  send_frame_header( struct net_device *, u32 * );
static int   skip_tail( unsigned int, unsigned int, u32 );
static int   check_fhdr( u32, u32 *, u32 *, u32 *, u32 *, u32 * );
static void  change_level( struct net_device * );
static void  timeout_change_level( struct net_device * );
static u32   calc_crc32( u32, u8 *, u32 );
static struct sk_buff *  get_rx_buf( struct net_device * );
static int  sbni_init( struct net_device * );

#ifdef CONFIG_SBNI_MULTILINE
static int  enslave( struct net_device *, struct net_device * );
static int  emancipate( struct net_device * );
#endif

static const char  version[] =
	"Granch SBNI12 driver ver 5.0.1  Jun 22 2001  Denis I.Timofeev.\n";

static bool skip_pci_probe	__initdata = false;
static int  scandone	__initdata = 0;
static int  num		__initdata = 0;

static unsigned char  rxl_tab[];
static u32  crc32tab[];

/* A list of all installed devices, for removing the driver module. */
static struct net_device  *sbni_cards[ SBNI_MAX_NUM_CARDS ];

/* Lists of device's parameters */
static u32	io[   SBNI_MAX_NUM_CARDS ] __initdata =
	{ [0 ... SBNI_MAX_NUM_CARDS-1] = -1 };
static u32	irq[  SBNI_MAX_NUM_CARDS ] __initdata;
static u32	baud[ SBNI_MAX_NUM_CARDS ] __initdata;
static u32	rxl[  SBNI_MAX_NUM_CARDS ] __initdata =
	{ [0 ... SBNI_MAX_NUM_CARDS-1] = -1 };
static u32	mac[  SBNI_MAX_NUM_CARDS ] __initdata;

#ifndef MODULE
typedef u32  iarr[];
static iarr *dest[5] __initdata = { &io, &irq, &baud, &rxl, &mac };
#endif

/* A zero-terminated list of I/O addresses to be probed on ISA bus */
static unsigned int  netcard_portlist[ ] __initdata = { 
	0x210, 0x214, 0x220, 0x224, 0x230, 0x234, 0x240, 0x244, 0x250, 0x254,
	0x260, 0x264, 0x270, 0x274, 0x280, 0x284, 0x290, 0x294, 0x2a0, 0x2a4,
	0x2b0, 0x2b4, 0x2c0, 0x2c4, 0x2d0, 0x2d4, 0x2e0, 0x2e4, 0x2f0, 0x2f4,
	0 };

#define NET_LOCAL_LOCK(dev) (((struct net_local *)netdev_priv(dev))->lock)

/*
 * Look for SBNI card which addr stored in dev->base_addr, if nonzero.
 * Otherwise, look through PCI bus. If none PCI-card was found, scan ISA.
 */

static inline int __init
sbni_isa_probe( struct net_device  *dev )
{
	if( dev->base_addr > 0x1ff &&
	    request_region( dev->base_addr, SBNI_IO_EXTENT, dev->name ) &&
	    sbni_probe1( dev, dev->base_addr, dev->irq ) )

		return  0;
	else {
		pr_err("base address 0x%lx is busy, or adapter is malfunctional!\n",
		       dev->base_addr);
		return  -ENODEV;
	}
}

static const struct net_device_ops sbni_netdev_ops = {
	.ndo_open		= sbni_open,
	.ndo_stop		= sbni_close,
	.ndo_start_xmit		= sbni_start_xmit,
	.ndo_set_rx_mode	= set_multicast_list,
	.ndo_do_ioctl		= sbni_ioctl,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static void __init sbni_devsetup(struct net_device *dev)
{
	ether_setup( dev );
	dev->netdev_ops = &sbni_netdev_ops;
}

int __init sbni_probe(int unit)
{
	struct net_device *dev;
	int err;

	dev = alloc_netdev(sizeof(struct net_local), "sbni",
			   NET_NAME_UNKNOWN, sbni_devsetup);
	if (!dev)
		return -ENOMEM;

	dev->netdev_ops = &sbni_netdev_ops;

	sprintf(dev->name, "sbni%d", unit);
	netdev_boot_setup_check(dev);

	err = sbni_init(dev);
	if (err) {
		free_netdev(dev);
		return err;
	}

	err = register_netdev(dev);
	if (err) {
		release_region( dev->base_addr, SBNI_IO_EXTENT );
		free_netdev(dev);
		return err;
	}
	pr_info_once("%s", version);
	return 0;
}

static int __init sbni_init(struct net_device *dev)
{
	int  i;
	if( dev->base_addr )
		return  sbni_isa_probe( dev );
	/* otherwise we have to perform search our adapter */

	if( io[ num ] != -1 )
		dev->base_addr	= io[ num ],
		dev->irq	= irq[ num ];
	else if( scandone  ||  io[ 0 ] != -1 )
		return  -ENODEV;

	/* if io[ num ] contains non-zero address, then that is on ISA bus */
	if( dev->base_addr )
		return  sbni_isa_probe( dev );

	/* ...otherwise - scan PCI first */
	if( !skip_pci_probe  &&  !sbni_pci_probe( dev ) )
		return  0;

	if( io[ num ] == -1 ) {
		/* Auto-scan will be stopped when first ISA card were found */
		scandone = 1;
		if( num > 0 )
			return  -ENODEV;
	}

	for( i = 0;  netcard_portlist[ i ];  ++i ) {
		int  ioaddr = netcard_portlist[ i ];
		if( request_region( ioaddr, SBNI_IO_EXTENT, dev->name ) &&
		    sbni_probe1( dev, ioaddr, 0 ))
			return 0;
	}

	return  -ENODEV;
}


static int __init
sbni_pci_probe( struct net_device  *dev )
{
	struct pci_dev  *pdev = NULL;

	while( (pdev = pci_get_class( PCI_CLASS_NETWORK_OTHER << 8, pdev ))
	       != NULL ) {
		int  pci_irq_line;
		unsigned long  pci_ioaddr;

		if( pdev->vendor != SBNI_PCI_VENDOR &&
		    pdev->device != SBNI_PCI_DEVICE )
			continue;

		pci_ioaddr = pci_resource_start( pdev, 0 );
		pci_irq_line = pdev->irq;

		/* Avoid already found cards from previous calls */
		if( !request_region( pci_ioaddr, SBNI_IO_EXTENT, dev->name ) ) {
			if (pdev->subsystem_device != 2)
				continue;

			/* Dual adapter is present */
			if (!request_region(pci_ioaddr += 4, SBNI_IO_EXTENT,
							dev->name ) )
				continue;
		}

		if (pci_irq_line <= 0 || pci_irq_line >= nr_irqs)
			pr_warn(
"WARNING: The PCI BIOS assigned this PCI card to IRQ %d, which is unlikely to work!.\n"
"You should use the PCI BIOS setup to assign a valid IRQ line.\n",
				pci_irq_line );

		/* avoiding re-enable dual adapters */
		if( (pci_ioaddr & 7) == 0  &&  pci_enable_device( pdev ) ) {
			release_region( pci_ioaddr, SBNI_IO_EXTENT );
			pci_dev_put( pdev );
			return  -EIO;
		}
		if( sbni_probe1( dev, pci_ioaddr, pci_irq_line ) ) {
			SET_NETDEV_DEV(dev, &pdev->dev);
			/* not the best thing to do, but this is all messed up 
			   for hotplug systems anyway... */
			pci_dev_put( pdev );
			return  0;
		}
	}
	return  -ENODEV;
}


static struct net_device * __init
sbni_probe1( struct net_device  *dev,  unsigned long  ioaddr,  int  irq )
{
	struct net_local  *nl;

	if( sbni_card_probe( ioaddr ) ) {
		release_region( ioaddr, SBNI_IO_EXTENT );
		return NULL;
	}

	outb( 0, ioaddr + CSR0 );

	if( irq < 2 ) {
		unsigned long irq_mask;

		irq_mask = probe_irq_on();
		outb( EN_INT | TR_REQ, ioaddr + CSR0 );
		outb( PR_RES, ioaddr + CSR1 );
		mdelay(50);
		irq = probe_irq_off(irq_mask);
		outb( 0, ioaddr + CSR0 );

		if( !irq ) {
			pr_err("%s: can't detect device irq!\n", dev->name);
			release_region( ioaddr, SBNI_IO_EXTENT );
			return NULL;
		}
	} else if( irq == 2 )
		irq = 9;

	dev->irq = irq;
	dev->base_addr = ioaddr;

	/* Fill in sbni-specific dev fields. */
	nl = netdev_priv(dev);
	if( !nl ) {
		pr_err("%s: unable to get memory!\n", dev->name);
		release_region( ioaddr, SBNI_IO_EXTENT );
		return NULL;
	}

	memset( nl, 0, sizeof(struct net_local) );
	spin_lock_init( &nl->lock );

	/* store MAC address (generate if that isn't known) */
	*(__be16 *)dev->dev_addr = htons( 0x00ff );
	*(__be32 *)(dev->dev_addr + 2) = htonl( 0x01000000 |
		((mac[num] ?
		mac[num] :
		(u32)((long)netdev_priv(dev))) & 0x00ffffff));

	/* store link settings (speed, receive level ) */
	nl->maxframe  = DEFAULT_FRAME_LEN;
	nl->csr1.rate = baud[ num ];

	if( (nl->cur_rxl_index = rxl[ num ]) == -1 )
		/* autotune rxl */
		nl->cur_rxl_index = DEF_RXL,
		nl->delta_rxl = DEF_RXL_DELTA;
	else
		nl->delta_rxl = 0;
	nl->csr1.rxl  = rxl_tab[ nl->cur_rxl_index ];
	if( inb( ioaddr + CSR0 ) & 0x01 )
		nl->state |= FL_SLOW_MODE;

	pr_notice("%s: ioaddr %#lx, irq %d, MAC: 00:ff:01:%02x:%02x:%02x\n",
		  dev->name, dev->base_addr, dev->irq,
		  ((u8 *)dev->dev_addr)[3],
		  ((u8 *)dev->dev_addr)[4],
		  ((u8 *)dev->dev_addr)[5]);

	pr_notice("%s: speed %d",
		  dev->name,
		  ((nl->state & FL_SLOW_MODE) ? 500000 : 2000000)
		  / (1 << nl->csr1.rate));

	if( nl->delta_rxl == 0 )
		pr_cont(", receive level 0x%x (fixed)\n", nl->cur_rxl_index);
	else
		pr_cont(", receive level (auto)\n");

#ifdef CONFIG_SBNI_MULTILINE
	nl->master = dev;
	nl->link   = NULL;
#endif
   
	sbni_cards[ num++ ] = dev;
	return  dev;
}

/* -------------------------------------------------------------------------- */

#ifdef CONFIG_SBNI_MULTILINE

static netdev_tx_t
sbni_start_xmit( struct sk_buff  *skb,  struct net_device  *dev )
{
	struct net_device  *p;

	netif_stop_queue( dev );

	/* Looking for idle device in the list */
	for( p = dev;  p; ) {
		struct net_local  *nl = netdev_priv(p);
		spin_lock( &nl->lock );
		if( nl->tx_buf_p  ||  (nl->state & FL_LINE_DOWN) ) {
			p = nl->link;
			spin_unlock( &nl->lock );
		} else {
			/* Idle dev is found */
			prepare_to_send( skb, p );
			spin_unlock( &nl->lock );
			netif_start_queue( dev );
			return NETDEV_TX_OK;
		}
	}

	return NETDEV_TX_BUSY;
}

#else	/* CONFIG_SBNI_MULTILINE */

static netdev_tx_t
sbni_start_xmit( struct sk_buff  *skb,  struct net_device  *dev )
{
	struct net_local  *nl  = netdev_priv(dev);

	netif_stop_queue( dev );
	spin_lock( &nl->lock );

	prepare_to_send( skb, dev );

	spin_unlock( &nl->lock );
	return NETDEV_TX_OK;
}

#endif	/* CONFIG_SBNI_MULTILINE */

/* -------------------------------------------------------------------------- */

/* interrupt handler */

/*
 * 	SBNI12D-10, -11/ISA boards within "common interrupt" mode could not
 * be looked as two independent single-channel devices. Every channel seems
 * as Ethernet interface but interrupt handler must be common. Really, first
 * channel ("master") driver only registers the handler. In its struct net_local
 * it has got pointer to "slave" channel's struct net_local and handles that's
 * interrupts too.
 *	dev of successfully attached ISA SBNI boards is linked to list.
 * While next board driver is initialized, it scans this list. If one
 * has found dev with same irq and ioaddr different by 4 then it assumes
 * this board to be "master".
 */ 

static irqreturn_t
sbni_interrupt( int  irq,  void  *dev_id )
{
	struct net_device	  *dev = dev_id;
	struct net_local  *nl  = netdev_priv(dev);
	int	repeat;

	spin_lock( &nl->lock );
	if( nl->second )
		spin_lock(&NET_LOCAL_LOCK(nl->second));

	do {
		repeat = 0;
		if( inb( dev->base_addr + CSR0 ) & (RC_RDY | TR_RDY) )
			handle_channel( dev ),
			repeat = 1;
		if( nl->second  && 	/* second channel present */
		    (inb( nl->second->base_addr+CSR0 ) & (RC_RDY | TR_RDY)) )
			handle_channel( nl->second ),
			repeat = 1;
	} while( repeat );

	if( nl->second )
		spin_unlock(&NET_LOCAL_LOCK(nl->second));
	spin_unlock( &nl->lock );
	return IRQ_HANDLED;
}


static void
handle_channel( struct net_device  *dev )
{
	struct net_local	*nl    = netdev_priv(dev);
	unsigned long		ioaddr = dev->base_addr;

	int  req_ans;
	unsigned char  csr0;

#ifdef CONFIG_SBNI_MULTILINE
	/* Lock the master device because we going to change its local data */
	if( nl->state & FL_SLAVE )
		spin_lock(&NET_LOCAL_LOCK(nl->master));
#endif

	outb( (inb( ioaddr + CSR0 ) & ~EN_INT) | TR_REQ, ioaddr + CSR0 );

	nl->timer_ticks = CHANGE_LEVEL_START_TICKS;
	for(;;) {
		csr0 = inb( ioaddr + CSR0 );
		if( ( csr0 & (RC_RDY | TR_RDY) ) == 0 )
			break;

		req_ans = !(nl->state & FL_PREV_OK);

		if( csr0 & RC_RDY )
			req_ans = recv_frame( dev );

		/*
		 * TR_RDY always equals 1 here because we have owned the marker,
		 * and we set TR_REQ when disabled interrupts
		 */
		csr0 = inb( ioaddr + CSR0 );
		if( !(csr0 & TR_RDY)  ||  (csr0 & RC_RDY) )
			netdev_err(dev, "internal error!\n");

		/* if state & FL_NEED_RESEND != 0 then tx_frameno != 0 */
		if( req_ans  ||  nl->tx_frameno != 0 )
			send_frame( dev );
		else
			/* send marker without any data */
			outb( inb( ioaddr + CSR0 ) & ~TR_REQ, ioaddr + CSR0 );
	}

	outb( inb( ioaddr + CSR0 ) | EN_INT, ioaddr + CSR0 );

#ifdef CONFIG_SBNI_MULTILINE
	if( nl->state & FL_SLAVE )
		spin_unlock(&NET_LOCAL_LOCK(nl->master));
#endif
}


/*
 * Routine returns 1 if it need to acknoweledge received frame.
 * Empty frame received without errors won't be acknoweledged.
 */

static int
recv_frame( struct net_device  *dev )
{
	struct net_local  *nl   = netdev_priv(dev);
	unsigned long  ioaddr	= dev->base_addr;

	u32  crc = CRC32_INITIAL;

	unsigned  framelen = 0, frameno, ack;
	unsigned  is_first, frame_ok = 0;

	if( check_fhdr( ioaddr, &framelen, &frameno, &ack, &is_first, &crc ) ) {
		frame_ok = framelen > 4
			?  upload_data( dev, framelen, frameno, is_first, crc )
			:  skip_tail( ioaddr, framelen, crc );
		if( frame_ok )
			interpret_ack( dev, ack );
	}

	outb( inb( ioaddr + CSR0 ) ^ CT_ZER, ioaddr + CSR0 );
	if( frame_ok ) {
		nl->state |= FL_PREV_OK;
		if( framelen > 4 )
			nl->in_stats.all_rx_number++;
	} else
		nl->state &= ~FL_PREV_OK,
		change_level( dev ),
		nl->in_stats.all_rx_number++,
		nl->in_stats.bad_rx_number++;

	return  !frame_ok  ||  framelen > 4;
}


static void
send_frame( struct net_device  *dev )
{
	struct net_local  *nl    = netdev_priv(dev);

	u32  crc = CRC32_INITIAL;

	if( nl->state & FL_NEED_RESEND ) {

		/* if frame was sended but not ACK'ed - resend it */
		if( nl->trans_errors ) {
			--nl->trans_errors;
			if( nl->framelen != 0 )
				nl->in_stats.resend_tx_number++;
		} else {
			/* cannot xmit with many attempts */
#ifdef CONFIG_SBNI_MULTILINE
			if( (nl->state & FL_SLAVE)  ||  nl->link )
#endif
			nl->state |= FL_LINE_DOWN;
			drop_xmit_queue( dev );
			goto  do_send;
		}
	} else
		nl->trans_errors = TR_ERROR_COUNT;

	send_frame_header( dev, &crc );
	nl->state |= FL_NEED_RESEND;
	/*
	 * FL_NEED_RESEND will be cleared after ACK, but if empty
	 * frame sended then in prepare_to_send next frame
	 */


	if( nl->framelen ) {
		download_data( dev, &crc );
		nl->in_stats.all_tx_number++;
		nl->state |= FL_WAIT_ACK;
	}

	outsb( dev->base_addr + DAT, (u8 *)&crc, sizeof crc );

do_send:
	outb( inb( dev->base_addr + CSR0 ) & ~TR_REQ, dev->base_addr + CSR0 );

	if( nl->tx_frameno )
		/* next frame exists - we request card to send it */
		outb( inb( dev->base_addr + CSR0 ) | TR_REQ,
		      dev->base_addr + CSR0 );
}


/*
 * Write the frame data into adapter's buffer memory, and calculate CRC.
 * Do padding if necessary.
 */

static void
download_data( struct net_device  *dev,  u32  *crc_p )
{
	struct net_local  *nl    = netdev_priv(dev);
	struct sk_buff    *skb	 = nl->tx_buf_p;

	unsigned  len = min_t(unsigned int, skb->len - nl->outpos, nl->framelen);

	outsb( dev->base_addr + DAT, skb->data + nl->outpos, len );
	*crc_p = calc_crc32( *crc_p, skb->data + nl->outpos, len );

	/* if packet too short we should write some more bytes to pad */
	for( len = nl->framelen - len;  len--; )
		outb( 0, dev->base_addr + DAT ),
		*crc_p = CRC32( 0, *crc_p );
}


static int
upload_data( struct net_device  *dev,  unsigned  framelen,  unsigned  frameno,
	     unsigned  is_first,  u32  crc )
{
	struct net_local  *nl = netdev_priv(dev);

	int  frame_ok;

	if( is_first )
		nl->wait_frameno = frameno,
		nl->inppos = 0;

	if( nl->wait_frameno == frameno ) {

		if( nl->inppos + framelen  <=  ETHER_MAX_LEN )
			frame_ok = append_frame_to_pkt( dev, framelen, crc );

		/*
		 * if CRC is right but framelen incorrect then transmitter
		 * error was occurred... drop entire packet
		 */
		else if( (frame_ok = skip_tail( dev->base_addr, framelen, crc ))
			 != 0 )
			nl->wait_frameno = 0,
			nl->inppos = 0,
#ifdef CONFIG_SBNI_MULTILINE
			nl->master->stats.rx_errors++,
			nl->master->stats.rx_missed_errors++;
#else
		        dev->stats.rx_errors++,
			dev->stats.rx_missed_errors++;
#endif
			/* now skip all frames until is_first != 0 */
	} else
		frame_ok = skip_tail( dev->base_addr, framelen, crc );

	if( is_first  &&  !frame_ok )
		/*
		 * Frame has been broken, but we had already stored
		 * is_first... Drop entire packet.
		 */
		nl->wait_frameno = 0,
#ifdef CONFIG_SBNI_MULTILINE
		nl->master->stats.rx_errors++,
		nl->master->stats.rx_crc_errors++;
#else
		dev->stats.rx_errors++,
		dev->stats.rx_crc_errors++;
#endif

	return  frame_ok;
}


static inline void
send_complete( struct net_device *dev )
{
	struct net_local  *nl = netdev_priv(dev);

#ifdef CONFIG_SBNI_MULTILINE
	nl->master->stats.tx_packets++;
	nl->master->stats.tx_bytes += nl->tx_buf_p->len;
#else
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += nl->tx_buf_p->len;
#endif
	dev_kfree_skb_irq( nl->tx_buf_p );

	nl->tx_buf_p = NULL;

	nl->outpos = 0;
	nl->state &= ~(FL_WAIT_ACK | FL_NEED_RESEND);
	nl->framelen   = 0;
}


static void
interpret_ack( struct net_device  *dev,  unsigned  ack )
{
	struct net_local  *nl = netdev_priv(dev);

	if( ack == FRAME_SENT_OK ) {
		nl->state &= ~FL_NEED_RESEND;

		if( nl->state & FL_WAIT_ACK ) {
			nl->outpos += nl->framelen;

			if( --nl->tx_frameno )
				nl->framelen = min_t(unsigned int,
						   nl->maxframe,
						   nl->tx_buf_p->len - nl->outpos);
			else
				send_complete( dev ),
#ifdef CONFIG_SBNI_MULTILINE
				netif_wake_queue( nl->master );
#else
				netif_wake_queue( dev );
#endif
		}
	}

	nl->state &= ~FL_WAIT_ACK;
}


/*
 * Glue received frame with previous fragments of packet.
 * Indicate packet when last frame would be accepted.
 */

static int
append_frame_to_pkt( struct net_device  *dev,  unsigned  framelen,  u32  crc )
{
	struct net_local  *nl = netdev_priv(dev);

	u8  *p;

	if( nl->inppos + framelen  >  ETHER_MAX_LEN )
		return  0;

	if( !nl->rx_buf_p  &&  !(nl->rx_buf_p = get_rx_buf( dev )) )
		return  0;

	p = nl->rx_buf_p->data + nl->inppos;
	insb( dev->base_addr + DAT, p, framelen );
	if( calc_crc32( crc, p, framelen ) != CRC32_REMAINDER )
		return  0;

	nl->inppos += framelen - 4;
	if( --nl->wait_frameno == 0 )		/* last frame received */
		indicate_pkt( dev );

	return  1;
}


/*
 * Prepare to start output on adapter.
 * Transmitter will be actually activated when marker is accepted.
 */

static void
prepare_to_send( struct sk_buff  *skb,  struct net_device  *dev )
{
	struct net_local  *nl = netdev_priv(dev);

	unsigned int  len;

	/* nl->tx_buf_p == NULL here! */
	if( nl->tx_buf_p )
		netdev_err(dev, "memory leak!\n");

	nl->outpos = 0;
	nl->state &= ~(FL_WAIT_ACK | FL_NEED_RESEND);

	len = skb->len;
	if( len < SBNI_MIN_LEN )
		len = SBNI_MIN_LEN;

	nl->tx_buf_p	= skb;
	nl->tx_frameno	= DIV_ROUND_UP(len, nl->maxframe);
	nl->framelen	= len < nl->maxframe  ?  len  :  nl->maxframe;

	outb( inb( dev->base_addr + CSR0 ) | TR_REQ,  dev->base_addr + CSR0 );
#ifdef CONFIG_SBNI_MULTILINE
	nl->master->trans_start = jiffies;
#else
	dev->trans_start = jiffies;
#endif
}


static void
drop_xmit_queue( struct net_device  *dev )
{
	struct net_local  *nl = netdev_priv(dev);

	if( nl->tx_buf_p )
		dev_kfree_skb_any( nl->tx_buf_p ),
		nl->tx_buf_p = NULL,
#ifdef CONFIG_SBNI_MULTILINE
		nl->master->stats.tx_errors++,
		nl->master->stats.tx_carrier_errors++;
#else
		dev->stats.tx_errors++,
		dev->stats.tx_carrier_errors++;
#endif

	nl->tx_frameno	= 0;
	nl->framelen	= 0;
	nl->outpos	= 0;
	nl->state &= ~(FL_WAIT_ACK | FL_NEED_RESEND);
#ifdef CONFIG_SBNI_MULTILINE
	netif_start_queue( nl->master );
	nl->master->trans_start = jiffies;
#else
	netif_start_queue( dev );
	dev->trans_start = jiffies;
#endif
}


static void
send_frame_header( struct net_device  *dev,  u32  *crc_p )
{
	struct net_local  *nl  = netdev_priv(dev);

	u32  crc = *crc_p;
	u32  len_field = nl->framelen + 6;	/* CRC + frameno + reserved */
	u8   value;

	if( nl->state & FL_NEED_RESEND )
		len_field |= FRAME_RETRY;	/* non-first attempt... */

	if( nl->outpos == 0 )
		len_field |= FRAME_FIRST;

	len_field |= (nl->state & FL_PREV_OK) ? FRAME_SENT_OK : FRAME_SENT_BAD;
	outb( SBNI_SIG, dev->base_addr + DAT );

	value = (u8) len_field;
	outb( value, dev->base_addr + DAT );
	crc = CRC32( value, crc );
	value = (u8) (len_field >> 8);
	outb( value, dev->base_addr + DAT );
	crc = CRC32( value, crc );

	outb( nl->tx_frameno, dev->base_addr + DAT );
	crc = CRC32( nl->tx_frameno, crc );
	outb( 0, dev->base_addr + DAT );
	crc = CRC32( 0, crc );
	*crc_p = crc;
}


/*
 * if frame tail not needed (incorrect number or received twice),
 * it won't store, but CRC will be calculated
 */

static int
skip_tail( unsigned int  ioaddr,  unsigned int  tail_len,  u32 crc )
{
	while( tail_len-- )
		crc = CRC32( inb( ioaddr + DAT ), crc );

	return  crc == CRC32_REMAINDER;
}


/*
 * Preliminary checks if frame header is correct, calculates its CRC
 * and split it to simple fields
 */

static int
check_fhdr( u32  ioaddr,  u32  *framelen,  u32  *frameno,  u32  *ack,
	    u32  *is_first,  u32  *crc_p )
{
	u32  crc = *crc_p;
	u8   value;

	if( inb( ioaddr + DAT ) != SBNI_SIG )
		return  0;

	value = inb( ioaddr + DAT );
	*framelen = (u32)value;
	crc = CRC32( value, crc );
	value = inb( ioaddr + DAT );
	*framelen |= ((u32)value) << 8;
	crc = CRC32( value, crc );

	*ack = *framelen & FRAME_ACK_MASK;
	*is_first = (*framelen & FRAME_FIRST) != 0;

	if( (*framelen &= FRAME_LEN_MASK) < 6 ||
	    *framelen > SBNI_MAX_FRAME - 3 )
		return  0;

	value = inb( ioaddr + DAT );
	*frameno = (u32)value;
	crc = CRC32( value, crc );

	crc = CRC32( inb( ioaddr + DAT ), crc );	/* reserved byte */
	*framelen -= 2;

	*crc_p = crc;
	return  1;
}


static struct sk_buff *
get_rx_buf( struct net_device  *dev )
{
	/* +2 is to compensate for the alignment fixup below */
	struct sk_buff  *skb = dev_alloc_skb( ETHER_MAX_LEN + 2 );
	if( !skb )
		return  NULL;

	skb_reserve( skb, 2 );		/* Align IP on longword boundaries */
	return  skb;
}


static void
indicate_pkt( struct net_device  *dev )
{
	struct net_local  *nl  = netdev_priv(dev);
	struct sk_buff    *skb = nl->rx_buf_p;

	skb_put( skb, nl->inppos );

#ifdef CONFIG_SBNI_MULTILINE
	skb->protocol = eth_type_trans( skb, nl->master );
	netif_rx( skb );
	++nl->master->stats.rx_packets;
	nl->master->stats.rx_bytes += nl->inppos;
#else
	skb->protocol = eth_type_trans( skb, dev );
	netif_rx( skb );
	++dev->stats.rx_packets;
	dev->stats.rx_bytes += nl->inppos;
#endif
	nl->rx_buf_p = NULL;	/* protocol driver will clear this sk_buff */
}


/* -------------------------------------------------------------------------- */

/*
 * Routine checks periodically wire activity and regenerates marker if
 * connect was inactive for a long time.
 */

static void
sbni_watchdog( unsigned long  arg )
{
	struct net_device  *dev = (struct net_device *) arg;
	struct net_local   *nl  = netdev_priv(dev);
	struct timer_list  *w   = &nl->watchdog; 
	unsigned long	   flags;
	unsigned char	   csr0;

	spin_lock_irqsave( &nl->lock, flags );

	csr0 = inb( dev->base_addr + CSR0 );
	if( csr0 & RC_CHK ) {

		if( nl->timer_ticks ) {
			if( csr0 & (RC_RDY | BU_EMP) )
				/* receiving not active */
				nl->timer_ticks--;
		} else {
			nl->in_stats.timeout_number++;
			if( nl->delta_rxl )
				timeout_change_level( dev );

			outb( *(u_char *)&nl->csr1 | PR_RES,
			      dev->base_addr + CSR1 );
			csr0 = inb( dev->base_addr + CSR0 );
		}
	} else
		nl->state &= ~FL_LINE_DOWN;

	outb( csr0 | RC_CHK, dev->base_addr + CSR0 ); 

	init_timer( w );
	w->expires	= jiffies + SBNI_TIMEOUT;
	w->data		= arg;
	w->function	= sbni_watchdog;
	add_timer( w );

	spin_unlock_irqrestore( &nl->lock, flags );
}


static unsigned char  rxl_tab[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08,
	0x0a, 0x0c, 0x0f, 0x16, 0x18, 0x1a, 0x1c, 0x1f
};

#define SIZE_OF_TIMEOUT_RXL_TAB 4
static unsigned char  timeout_rxl_tab[] = {
	0x03, 0x05, 0x08, 0x0b
};

/* -------------------------------------------------------------------------- */

static void
card_start( struct net_device  *dev )
{
	struct net_local  *nl = netdev_priv(dev);

	nl->timer_ticks = CHANGE_LEVEL_START_TICKS;
	nl->state &= ~(FL_WAIT_ACK | FL_NEED_RESEND);
	nl->state |= FL_PREV_OK;

	nl->inppos = nl->outpos = 0;
	nl->wait_frameno = 0;
	nl->tx_frameno	 = 0;
	nl->framelen	 = 0;

	outb( *(u_char *)&nl->csr1 | PR_RES, dev->base_addr + CSR1 );
	outb( EN_INT, dev->base_addr + CSR0 );
}

/* -------------------------------------------------------------------------- */

/* Receive level auto-selection */

static void
change_level( struct net_device  *dev )
{
	struct net_local  *nl = netdev_priv(dev);

	if( nl->delta_rxl == 0 )	/* do not auto-negotiate RxL */
		return;

	if( nl->cur_rxl_index == 0 )
		nl->delta_rxl = 1;
	else if( nl->cur_rxl_index == 15 )
		nl->delta_rxl = -1;
	else if( nl->cur_rxl_rcvd < nl->prev_rxl_rcvd )
		nl->delta_rxl = -nl->delta_rxl;

	nl->csr1.rxl = rxl_tab[ nl->cur_rxl_index += nl->delta_rxl ];
	inb( dev->base_addr + CSR0 );	/* needs for PCI cards */
	outb( *(u8 *)&nl->csr1, dev->base_addr + CSR1 );

	nl->prev_rxl_rcvd = nl->cur_rxl_rcvd;
	nl->cur_rxl_rcvd  = 0;
}


static void
timeout_change_level( struct net_device  *dev )
{
	struct net_local  *nl = netdev_priv(dev);

	nl->cur_rxl_index = timeout_rxl_tab[ nl->timeout_rxl ];
	if( ++nl->timeout_rxl >= 4 )
		nl->timeout_rxl = 0;

	nl->csr1.rxl = rxl_tab[ nl->cur_rxl_index ];
	inb( dev->base_addr + CSR0 );
	outb( *(unsigned char *)&nl->csr1, dev->base_addr + CSR1 );

	nl->prev_rxl_rcvd = nl->cur_rxl_rcvd;
	nl->cur_rxl_rcvd  = 0;
}

/* -------------------------------------------------------------------------- */

/*
 *	Open/initialize the board. 
 */

static int
sbni_open( struct net_device  *dev )
{
	struct net_local	*nl = netdev_priv(dev);
	struct timer_list	*w  = &nl->watchdog;

	/*
	 * For double ISA adapters within "common irq" mode, we have to
	 * determine whether primary or secondary channel is initialized,
	 * and set the irq handler only in first case.
	 */
	if( dev->base_addr < 0x400 ) {		/* ISA only */
		struct net_device  **p = sbni_cards;
		for( ;  *p  &&  p < sbni_cards + SBNI_MAX_NUM_CARDS;  ++p )
			if( (*p)->irq == dev->irq &&
			    ((*p)->base_addr == dev->base_addr + 4 ||
			     (*p)->base_addr == dev->base_addr - 4) &&
			    (*p)->flags & IFF_UP ) {

				((struct net_local *) (netdev_priv(*p)))
					->second = dev;
				netdev_notice(dev, "using shared irq with %s\n",
					      (*p)->name);
				nl->state |= FL_SECONDARY;
				goto  handler_attached;
			}
	}

	if( request_irq(dev->irq, sbni_interrupt, IRQF_SHARED, dev->name, dev) ) {
		netdev_err(dev, "unable to get IRQ %d\n", dev->irq);
		return  -EAGAIN;
	}

handler_attached:

	spin_lock( &nl->lock );
	memset( &dev->stats, 0, sizeof(struct net_device_stats) );
	memset( &nl->in_stats, 0, sizeof(struct sbni_in_stats) );

	card_start( dev );

	netif_start_queue( dev );

	/* set timer watchdog */
	init_timer( w );
	w->expires	= jiffies + SBNI_TIMEOUT;
	w->data		= (unsigned long) dev;
	w->function	= sbni_watchdog;
	add_timer( w );
   
	spin_unlock( &nl->lock );
	return 0;
}


static int
sbni_close( struct net_device  *dev )
{
	struct net_local  *nl = netdev_priv(dev);

	if( nl->second  &&  nl->second->flags & IFF_UP ) {
		netdev_notice(dev, "Secondary channel (%s) is active!\n",
			      nl->second->name);
		return  -EBUSY;
	}

#ifdef CONFIG_SBNI_MULTILINE
	if( nl->state & FL_SLAVE )
		emancipate( dev );
	else
		while( nl->link )	/* it's master device! */
			emancipate( nl->link );
#endif

	spin_lock( &nl->lock );

	nl->second = NULL;
	drop_xmit_queue( dev );	
	netif_stop_queue( dev );
   
	del_timer( &nl->watchdog );

	outb( 0, dev->base_addr + CSR0 );

	if( !(nl->state & FL_SECONDARY) )
		free_irq( dev->irq, dev );
	nl->state &= FL_SECONDARY;

	spin_unlock( &nl->lock );
	return 0;
}


/*
	Valid combinations in CSR0 (for probing):

	VALID_DECODER	0000,0011,1011,1010

				    	; 0   ; -
				TR_REQ	; 1   ; +
			TR_RDY	    	; 2   ; -
			TR_RDY	TR_REQ	; 3   ; +
		BU_EMP		    	; 4   ; +
		BU_EMP	     	TR_REQ	; 5   ; +
		BU_EMP	TR_RDY	    	; 6   ; -
		BU_EMP	TR_RDY	TR_REQ	; 7   ; +
	RC_RDY 		     		; 8   ; +
	RC_RDY			TR_REQ	; 9   ; +
	RC_RDY		TR_RDY		; 10  ; -
	RC_RDY		TR_RDY	TR_REQ	; 11  ; -
	RC_RDY	BU_EMP			; 12  ; -
	RC_RDY	BU_EMP		TR_REQ	; 13  ; -
	RC_RDY	BU_EMP	TR_RDY		; 14  ; -
	RC_RDY	BU_EMP	TR_RDY	TR_REQ	; 15  ; -
*/

#define VALID_DECODER (2 + 8 + 0x10 + 0x20 + 0x80 + 0x100 + 0x200)


static int
sbni_card_probe( unsigned long  ioaddr )
{
	unsigned char  csr0;

	csr0 = inb( ioaddr + CSR0 );
	if( csr0 != 0xff  &&  csr0 != 0x00 ) {
		csr0 &= ~EN_INT;
		if( csr0 & BU_EMP )
			csr0 |= EN_INT;
      
		if( VALID_DECODER & (1 << (csr0 >> 4)) )
			return  0;
	}
   
	return  -ENODEV;
}

/* -------------------------------------------------------------------------- */

static int
sbni_ioctl( struct net_device  *dev,  struct ifreq  *ifr,  int  cmd )
{
	struct net_local  *nl = netdev_priv(dev);
	struct sbni_flags  flags;
	int  error = 0;

#ifdef CONFIG_SBNI_MULTILINE
	struct net_device  *slave_dev;
	char  slave_name[ 8 ];
#endif
  
	switch( cmd ) {
	case  SIOCDEVGETINSTATS :
		if (copy_to_user( ifr->ifr_data, &nl->in_stats,
					sizeof(struct sbni_in_stats) ))
			error = -EFAULT;
		break;

	case  SIOCDEVRESINSTATS :
		if (!capable(CAP_NET_ADMIN))
			return  -EPERM;
		memset( &nl->in_stats, 0, sizeof(struct sbni_in_stats) );
		break;

	case  SIOCDEVGHWSTATE :
		flags.mac_addr	= *(u32 *)(dev->dev_addr + 3);
		flags.rate	= nl->csr1.rate;
		flags.slow_mode	= (nl->state & FL_SLOW_MODE) != 0;
		flags.rxl	= nl->cur_rxl_index;
		flags.fixed_rxl	= nl->delta_rxl == 0;

		if (copy_to_user( ifr->ifr_data, &flags, sizeof flags ))
			error = -EFAULT;
		break;

	case  SIOCDEVSHWSTATE :
		if (!capable(CAP_NET_ADMIN))
			return  -EPERM;

		spin_lock( &nl->lock );
		flags = *(struct sbni_flags*) &ifr->ifr_ifru;
		if( flags.fixed_rxl )
			nl->delta_rxl = 0,
			nl->cur_rxl_index = flags.rxl;
		else
			nl->delta_rxl = DEF_RXL_DELTA,
			nl->cur_rxl_index = DEF_RXL;

		nl->csr1.rxl = rxl_tab[ nl->cur_rxl_index ];
		nl->csr1.rate = flags.rate;
		outb( *(u8 *)&nl->csr1 | PR_RES, dev->base_addr + CSR1 );
		spin_unlock( &nl->lock );
		break;

#ifdef CONFIG_SBNI_MULTILINE

	case  SIOCDEVENSLAVE :
		if (!capable(CAP_NET_ADMIN))
			return  -EPERM;

		if (copy_from_user( slave_name, ifr->ifr_data, sizeof slave_name ))
			return -EFAULT;
		slave_dev = dev_get_by_name(&init_net, slave_name );
		if( !slave_dev  ||  !(slave_dev->flags & IFF_UP) ) {
			netdev_err(dev, "trying to enslave non-active device %s\n",
				   slave_name);
			if (slave_dev)
				dev_put(slave_dev);
			return  -EPERM;
		}

		return  enslave( dev, slave_dev );

	case  SIOCDEVEMANSIPATE :
		if (!capable(CAP_NET_ADMIN))
			return  -EPERM;

		return  emancipate( dev );

#endif	/* CONFIG_SBNI_MULTILINE */

	default :
		return  -EOPNOTSUPP;
	}

	return  error;
}


#ifdef CONFIG_SBNI_MULTILINE

static int
enslave( struct net_device  *dev,  struct net_device  *slave_dev )
{
	struct net_local  *nl  = netdev_priv(dev);
	struct net_local  *snl = netdev_priv(slave_dev);

	if( nl->state & FL_SLAVE )	/* This isn't master or free device */
		return  -EBUSY;

	if( snl->state & FL_SLAVE )	/* That was already enslaved */
		return  -EBUSY;

	spin_lock( &nl->lock );
	spin_lock( &snl->lock );

	/* append to list */
	snl->link = nl->link;
	nl->link  = slave_dev;
	snl->master = dev;
	snl->state |= FL_SLAVE;

	/* Summary statistics of MultiLine operation will be stored
	   in master's counters */
	memset( &slave_dev->stats, 0, sizeof(struct net_device_stats) );
	netif_stop_queue( slave_dev );
	netif_wake_queue( dev );	/* Now we are able to transmit */

	spin_unlock( &snl->lock );
	spin_unlock( &nl->lock );
	netdev_notice(dev, "slave device (%s) attached\n", slave_dev->name);
	return  0;
}


static int
emancipate( struct net_device  *dev )
{
	struct net_local   *snl = netdev_priv(dev);
	struct net_device  *p   = snl->master;
	struct net_local   *nl  = netdev_priv(p);

	if( !(snl->state & FL_SLAVE) )
		return  -EINVAL;

	spin_lock( &nl->lock );
	spin_lock( &snl->lock );
	drop_xmit_queue( dev );

	/* exclude from list */
	for(;;) {	/* must be in list */
		struct net_local  *t = netdev_priv(p);
		if( t->link == dev ) {
			t->link = snl->link;
			break;
		}
		p = t->link;
	}

	snl->link = NULL;
	snl->master = dev;
	snl->state &= ~FL_SLAVE;

	netif_start_queue( dev );

	spin_unlock( &snl->lock );
	spin_unlock( &nl->lock );

	dev_put( dev );
	return  0;
}

#endif

static void
set_multicast_list( struct net_device  *dev )
{
	return;		/* sbni always operate in promiscuos mode */
}


#ifdef MODULE
module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(baud, int, NULL, 0);
module_param_array(rxl, int, NULL, 0);
module_param_array(mac, int, NULL, 0);
module_param(skip_pci_probe, bool, 0);

MODULE_LICENSE("GPL");


int __init init_module( void )
{
	struct net_device  *dev;
	int err;

	while( num < SBNI_MAX_NUM_CARDS ) {
		dev = alloc_netdev(sizeof(struct net_local), "sbni%d",
				   NET_NAME_UNKNOWN, sbni_devsetup);
		if( !dev)
			break;

		sprintf( dev->name, "sbni%d", num );

		err = sbni_init(dev);
		if (err) {
			free_netdev(dev);
			break;
		}

		if( register_netdev( dev ) ) {
			release_region( dev->base_addr, SBNI_IO_EXTENT );
			free_netdev( dev );
			break;
		}
	}

	return  *sbni_cards  ?  0  :  -ENODEV;
}

void
cleanup_module(void)
{
	int i;

	for (i = 0;  i < SBNI_MAX_NUM_CARDS;  ++i) {
		struct net_device *dev = sbni_cards[i];
		if (dev != NULL) {
			unregister_netdev(dev);
			release_region(dev->base_addr, SBNI_IO_EXTENT);
			free_netdev(dev);
		}
	}
}

#else	/* MODULE */

static int __init
sbni_setup( char  *p )
{
	int  n, parm;

	if( *p++ != '(' )
		goto  bad_param;

	for( n = 0, parm = 0;  *p  &&  n < 8; ) {
		(*dest[ parm ])[ n ] = simple_strtol( p, &p, 0 );
		if( !*p  ||  *p == ')' )
			return 1;
		if( *p == ';' )
			++p, ++n, parm = 0;
		else if( *p++ != ',' )
			break;
		else
			if( ++parm >= 5 )
				break;
	}
bad_param:
	pr_err("Error in sbni kernel parameter!\n");
	return 0;
}

__setup( "sbni=", sbni_setup );

#endif	/* MODULE */

/* -------------------------------------------------------------------------- */

static u32
calc_crc32( u32  crc,  u8  *p,  u32  len )
{
	while( len-- )
		crc = CRC32( *p++, crc );

	return  crc;
}

static u32  crc32tab[] __attribute__ ((aligned(8))) = {
	0xD202EF8D,  0xA505DF1B,  0x3C0C8EA1,  0x4B0BBE37,
	0xD56F2B94,  0xA2681B02,  0x3B614AB8,  0x4C667A2E,
	0xDCD967BF,  0xABDE5729,  0x32D70693,  0x45D03605,
	0xDBB4A3A6,  0xACB39330,  0x35BAC28A,  0x42BDF21C,
	0xCFB5FFE9,  0xB8B2CF7F,  0x21BB9EC5,  0x56BCAE53,
	0xC8D83BF0,  0xBFDF0B66,  0x26D65ADC,  0x51D16A4A,
	0xC16E77DB,  0xB669474D,  0x2F6016F7,  0x58672661,
	0xC603B3C2,  0xB1048354,  0x280DD2EE,  0x5F0AE278,
	0xE96CCF45,  0x9E6BFFD3,  0x0762AE69,  0x70659EFF,
	0xEE010B5C,  0x99063BCA,  0x000F6A70,  0x77085AE6,
	0xE7B74777,  0x90B077E1,  0x09B9265B,  0x7EBE16CD,
	0xE0DA836E,  0x97DDB3F8,  0x0ED4E242,  0x79D3D2D4,
	0xF4DBDF21,  0x83DCEFB7,  0x1AD5BE0D,  0x6DD28E9B,
	0xF3B61B38,  0x84B12BAE,  0x1DB87A14,  0x6ABF4A82,
	0xFA005713,  0x8D076785,  0x140E363F,  0x630906A9,
	0xFD6D930A,  0x8A6AA39C,  0x1363F226,  0x6464C2B0,
	0xA4DEAE1D,  0xD3D99E8B,  0x4AD0CF31,  0x3DD7FFA7,
	0xA3B36A04,  0xD4B45A92,  0x4DBD0B28,  0x3ABA3BBE,
	0xAA05262F,  0xDD0216B9,  0x440B4703,  0x330C7795,
	0xAD68E236,  0xDA6FD2A0,  0x4366831A,  0x3461B38C,
	0xB969BE79,  0xCE6E8EEF,  0x5767DF55,  0x2060EFC3,
	0xBE047A60,  0xC9034AF6,  0x500A1B4C,  0x270D2BDA,
	0xB7B2364B,  0xC0B506DD,  0x59BC5767,  0x2EBB67F1,
	0xB0DFF252,  0xC7D8C2C4,  0x5ED1937E,  0x29D6A3E8,
	0x9FB08ED5,  0xE8B7BE43,  0x71BEEFF9,  0x06B9DF6F,
	0x98DD4ACC,  0xEFDA7A5A,  0x76D32BE0,  0x01D41B76,
	0x916B06E7,  0xE66C3671,  0x7F6567CB,  0x0862575D,
	0x9606C2FE,  0xE101F268,  0x7808A3D2,  0x0F0F9344,
	0x82079EB1,  0xF500AE27,  0x6C09FF9D,  0x1B0ECF0B,
	0x856A5AA8,  0xF26D6A3E,  0x6B643B84,  0x1C630B12,
	0x8CDC1683,  0xFBDB2615,  0x62D277AF,  0x15D54739,
	0x8BB1D29A,  0xFCB6E20C,  0x65BFB3B6,  0x12B88320,
	0x3FBA6CAD,  0x48BD5C3B,  0xD1B40D81,  0xA6B33D17,
	0x38D7A8B4,  0x4FD09822,  0xD6D9C998,  0xA1DEF90E,
	0x3161E49F,  0x4666D409,  0xDF6F85B3,  0xA868B525,
	0x360C2086,  0x410B1010,  0xD80241AA,  0xAF05713C,
	0x220D7CC9,  0x550A4C5F,  0xCC031DE5,  0xBB042D73,
	0x2560B8D0,  0x52678846,  0xCB6ED9FC,  0xBC69E96A,
	0x2CD6F4FB,  0x5BD1C46D,  0xC2D895D7,  0xB5DFA541,
	0x2BBB30E2,  0x5CBC0074,  0xC5B551CE,  0xB2B26158,
	0x04D44C65,  0x73D37CF3,  0xEADA2D49,  0x9DDD1DDF,
	0x03B9887C,  0x74BEB8EA,  0xEDB7E950,  0x9AB0D9C6,
	0x0A0FC457,  0x7D08F4C1,  0xE401A57B,  0x930695ED,
	0x0D62004E,  0x7A6530D8,  0xE36C6162,  0x946B51F4,
	0x19635C01,  0x6E646C97,  0xF76D3D2D,  0x806A0DBB,
	0x1E0E9818,  0x6909A88E,  0xF000F934,  0x8707C9A2,
	0x17B8D433,  0x60BFE4A5,  0xF9B6B51F,  0x8EB18589,
	0x10D5102A,  0x67D220BC,  0xFEDB7106,  0x89DC4190,
	0x49662D3D,  0x3E611DAB,  0xA7684C11,  0xD06F7C87,
	0x4E0BE924,  0x390CD9B2,  0xA0058808,  0xD702B89E,
	0x47BDA50F,  0x30BA9599,  0xA9B3C423,  0xDEB4F4B5,
	0x40D06116,  0x37D75180,  0xAEDE003A,  0xD9D930AC,
	0x54D13D59,  0x23D60DCF,  0xBADF5C75,  0xCDD86CE3,
	0x53BCF940,  0x24BBC9D6,  0xBDB2986C,  0xCAB5A8FA,
	0x5A0AB56B,  0x2D0D85FD,  0xB404D447,  0xC303E4D1,
	0x5D677172,  0x2A6041E4,  0xB369105E,  0xC46E20C8,
	0x72080DF5,  0x050F3D63,  0x9C066CD9,  0xEB015C4F,
	0x7565C9EC,  0x0262F97A,  0x9B6BA8C0,  0xEC6C9856,
	0x7CD385C7,  0x0BD4B551,  0x92DDE4EB,  0xE5DAD47D,
	0x7BBE41DE,  0x0CB97148,  0x95B020F2,  0xE2B71064,
	0x6FBF1D91,  0x18B82D07,  0x81B17CBD,  0xF6B64C2B,
	0x68D2D988,  0x1FD5E91E,  0x86DCB8A4,  0xF1DB8832,
	0x616495A3,  0x1663A535,  0x8F6AF48F,  0xF86DC419,
	0x660951BA,  0x110E612C,  0x88073096,  0xFF000000
};

